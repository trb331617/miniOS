#include "keyboard.h"
#include "interrupt.h"
#include "print.h"
#include "io.h"
#include "global.h"
#include "ioqueue.h"

// 键盘buffer寄存器端口号为0x60
// 8042芯片的输入和输出缓冲区寄存器端口
#define KEYBOARD_BUFFER_PORT    0x60

// 键盘上8048芯片 -> 主板上8042芯片 -> 中断代理8259A


/* 用转义字符定义部分控制字符 */
#define esc         '\033'  // 八进制表示，也可用十六机制'\x1b'
#define backspace   '\b'
#define tab         '\t'
#define enter       '\r'
#define delete      '\177'  // 八进制表示，也可用十六进制'\x7f'

/* 以下不可见字符一律定义为0 */
#define char_invisible  0
#define ctrl_l_char     char_invisible
#define ctrl_r_char     char_invisible
#define shift_l_char    char_invisible
#define shift_r_char    char_invisible
#define alt_l_char      char_invisible
#define alt_r_char      char_invisible
#define caps_lock_char  char_invisible

/* 定义控制字符的通码和断码 */
#define shift_l_make    0x2a
#define shift_r_make    0x36
#define alt_l_make      0x38
#define alt_r_make      0xe038
#define alt_r_break     0xe0b8
#define ctrl_l_make     0x1d
#define ctrl_r_make     0xe01d
#define ctrl_r_break    0xe09d
#define caps_lock_make  0x3a    


struct ioqueue keyboard_buf;    // 定义键盘的环形缓冲区


/* 定义以下全局变量，记录相应键是否为按下的状态 */
// ext_scancode用于记录makecode是否以0xe0开头
// ctrl_status, shift_status, alt_status记录这3个键是否被按下并且尚未松开
// ext_scancode若扫描码是e0开头的，表示此键的按下将产生多个扫描码，后面还有扫描码
static bool ctrl_status, shift_status, alt_status, caps_lock_status, ext_scancode;


/* 以通码make_code为索引的二维数组 */
static char keymap[][2] = {
/* 扫描码   未与shift组合  与shift组合*/
/* ---------------------------------- */
/* 0x00 */	{0,	0},		
/* 0x01 */	{esc,	esc},		
/* 0x02 */	{'1',	'!'},		
/* 0x03 */	{'2',	'@'},		
/* 0x04 */	{'3',	'#'},		
/* 0x05 */	{'4',	'$'},		
/* 0x06 */	{'5',	'%'},		
/* 0x07 */	{'6',	'^'},		
/* 0x08 */	{'7',	'&'},		
/* 0x09 */	{'8',	'*'},		
/* 0x0A */	{'9',	'('},		
/* 0x0B */	{'0',	')'},		
/* 0x0C */	{'-',	'_'},		
/* 0x0D */	{'=',	'+'},		
/* 0x0E */	{backspace, backspace},	
/* 0x0F */	{tab,	tab},		
/* 0x10 */	{'q',	'Q'},		
/* 0x11 */	{'w',	'W'},		
/* 0x12 */	{'e',	'E'},		
/* 0x13 */	{'r',	'R'},		
/* 0x14 */	{'t',	'T'},		
/* 0x15 */	{'y',	'Y'},		
/* 0x16 */	{'u',	'U'},		
/* 0x17 */	{'i',	'I'},		
/* 0x18 */	{'o',	'O'},		
/* 0x19 */	{'p',	'P'},		
/* 0x1A */	{'[',	'{'},		
/* 0x1B */	{']',	'}'},		
/* 0x1C */	{enter,  enter},
/* 0x1D */	{ctrl_l_char, ctrl_l_char},
/* 0x1E */	{'a',	'A'},		
/* 0x1F */	{'s',	'S'},		
/* 0x20 */	{'d',	'D'},		
/* 0x21 */	{'f',	'F'},		
/* 0x22 */	{'g',	'G'},		
/* 0x23 */	{'h',	'H'},		
/* 0x24 */	{'j',	'J'},		
/* 0x25 */	{'k',	'K'},		
/* 0x26 */	{'l',	'L'},		
/* 0x27 */	{';',	':'},		
/* 0x28 */	{'\'',	'"'},		
/* 0x29 */	{'`',	'~'},		
/* 0x2A */	{shift_l_char, shift_l_char},	
/* 0x2B */	{'\\',	'|'},		
/* 0x2C */	{'z',	'Z'},		
/* 0x2D */	{'x',	'X'},		
/* 0x2E */	{'c',	'C'},		
/* 0x2F */	{'v',	'V'},		
/* 0x30 */	{'b',	'B'},		
/* 0x31 */	{'n',	'N'},		
/* 0x32 */	{'m',	'M'},		
/* 0x33 */	{',',	'<'},		
/* 0x34 */	{'.',	'>'},		
/* 0x35 */	{'/',	'?'},
/* 0x36	*/	{shift_r_char, shift_r_char},	
/* 0x37 */	{'*',	'*'},    	
/* 0x38 */	{alt_l_char, alt_l_char},
/* 0x39 */	{' ',	' '},		
/* 0x3A */	{caps_lock_char, caps_lock_char}
/*其它按键暂不处理*/
};



/* 键盘中断处理程序 */
// 每次处理一个字节，所以当扫描码中是多字节/或者有组合键时，需要定义额外的全局变量来记录曾经被按下
static void intr_keyboard_handler(void)
{
    // 这次中断发生前的上一次中断，以下任意3个键是否有按下
    bool ctrl_down_last = ctrl_status;
    bool shift_down_last = shift_status;
    bool caps_lock_last = caps_lock_status;
    
    bool break_code;
    
    // 必须要读取输出缓冲区寄存器，否则8042芯片不再继续响应键盘中断
    // 如果不读取输出缓冲区寄存器，8042是不会继续工作的
    unsigned short int scancode = inb(KEYBOARD_BUFFER_PORT); // 从输出缓冲区寄存器读取扫描码
    
    // 若扫描码是e0开头的，表示此键的按下将产生多个扫描码，后面还有扫描码
    // 所以马上结束此次中断处理函数，等待下一个扫描码进来
    if(scancode == 0xe0)
    {
        ext_scancode = true;    // 打开e0标记
        return;
    }
    
    if(ext_scancode)
    {
        scancode = (0xe000 | scancode);
        ext_scancode = false;   // 关闭e0标记
    }
    
    // 判断扫描码是否为断码，第一套键盘扫描码中断码的第8位为1，通码的为0
    break_code = ((scancode & 0x0080) != 0);    // 获取break_code
    
    // 断码break_code
    // (按键弹起时产生的扫描码)
    if(break_code)      
    {
        // 由于ctrl_r alt_r的make_code和break_code都是2字节，
        // 所以可用下面的方法取make_code，多字节的扫描码暂不处理
        
        // 为了检索数组keymap得到此次按键对应的字符，需要将接收到的断码还原为通码
        // 第一套键盘扫描码中断码的第8位为1，通码的为0
        unsigned short int make_code = (scancode &= 0xff7f);   // 得到此次按键(扫描码)对应的字符是什么
      
      
        // 若是任意以下3个键(ctrl shift alt)弹起了，将状态置为false
        // 由于caps_lock不是弹起后关闭，所以需要单独处理
        if(make_code == ctrl_l_make || make_code == ctrl_r_make)
            ctrl_status = false;
        else if(make_code == shift_l_make || make_code == shift_r_make)
            shift_status = false;
        else if(make_code == alt_l_make || make_code == alt_r_make)
            alt_status = false;
        
        // 这3个键的状态变量并不是本次使用，是供下次判断组合键用的
        // 这里只是记录是否松开了它们
        return;     // 直接返回，结束此次中断处理程序
    }
    // 通码make_code
    // 只处理数组中定义的键以及alt_right和ctrl键
    // 根据通码和shift键是否按下的情况，在数组keymap中找到按键对应的字符
    else if((scancode > 0x00 && scancode < 0x3b) || \
            (scancode == alt_r_make) || \
            (scancode == ctrl_r_make))
    {
        
        // 判断是否与shift组合，用来在数组中索引对应的字符
        bool shift= false;  
        if ((scancode < 0x0e) || (scancode == 0x29) || \
            (scancode == 0x1a) || (scancode == 0x1b) || \
            (scancode == 0x2b) || (scancode == 0x27) || \
            (scancode == 0x28) || (scancode == 0x33) || \
            (scancode == 0x34) || (scancode == 0x35))
        {
            /****** 代表两个字母的键 ********
		     0x0e 数字'0'~'9',字符'-',字符'='
		     0x29 字符'`'
		     0x1a 字符'['
		     0x1b 字符']'
		     0x2b 字符'\\'
		     0x27 字符';'
		     0x28 字符'\''
		     0x33 字符','
		     0x34 字符'.'
		     0x35 字符'/' 
            *******************************/
            if(shift_down_last)     // 若同时按下了shift键
                shift = true;
        }
        else    // 默认为字母键
        {
            if(shift_down_last && caps_lock_last) // 如果shift和capslock同时按下, 功能抵消, 为小写
                shift = false;
            else if(shift_down_last || caps_lock_last) // 如果shift和capslock任意被按下
                shift = true;
            else
                shift = false;
        }

    
        // 将扫描码的高字节置0，主要是针对高字节是e0的扫描码
        unsigned char index = (scancode &= 0x00ff);

        char current_char = keymap[index][shift];   // 在数组中找到对应的字符
        
        if(current_char)    // 只处理ascii码不为0的键
        {
            
            /*****************  快捷键ctrl+l和ctrl+u的处理 *********************
            * 下面是把ctrl+l和ctrl+u这两种组合键产生的字符置为:
            * cur_char的asc码 - 字符a的asc码, 此差值比较小,
            * 属于asc码表中不可见的字符部分.故不会产生可见字符.
            * 我们在shell中将ascii值为l-a和u-a的分别处理为清屏和删除输入的快捷键*/
            if ((ctrl_down_last && current_char == 'l') || (ctrl_down_last && current_char == 'u'))
                current_char -= 'a';
            /****************************************************************/            
            
            // 若keyboard_buf未满，则将其加入到缓冲区
            if(!ioq_full(&keyboard_buf))
            {
                // put_char(current_char);     // 临时的
                ioq_putchar(&keyboard_buf, current_char);
            }
            return;
        }
        
        /* 记录本次是否按下了下面几类控制键之一,供下次键入时判断组合键 */
        // 若current_char为0，根据前面keymap的定义，说明是操作控制键ctrl shift alt capslock
        if(scancode == ctrl_l_make || scancode == ctrl_r_make)
            ctrl_status = true;
        else if(scancode == shift_l_make || scancode == shift_r_make)
            shift_status = true;
        else if(scancode == alt_l_make || scancode == alt_r_make)
            alt_status = true;
        else if(scancode == caps_lock_make)
            caps_lock_status = !caps_lock_status;
    }
    else
        put_str("[ERROR]unknown key\n\n");    
}

/* 键盘初始化 */
void keyboard_init()
{
    put_str("keyboard init begin...");
    
    ioqueue_init(&keyboard_buf);    // 键盘的环形缓冲区
    register_handler(0x21, intr_keyboard_handler);
    
    // put_str("keyboard init done\n");
    put_str(" done!\n");
}