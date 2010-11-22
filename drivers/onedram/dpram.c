/****************************************************************************
**
** COPYRIGHT(C) : Samsung Electronics Co.Ltd, 2006-2010 ALL RIGHTS RESERVED
**
**                Onedram Device Driver
**
**	when		who(SingleID)	what,where,why
**--------------------------------------------------------------------------
**	091100		kt.hur			arrange dpram driver source
****************************************************************************/

/*--------------------------------------------------------------------------- 
 * DPRAM drvier version,
 * See the "IPC Overview Specification" document 
 --------------------------------------------------------------------------*/

/*
#undef CONFIG_ONEDRAM_CHECKBIT
#undef CONFIG_ONEDRAM_IRQPENDING

#define	CONFIG_ONEDRAM_TX_RETRY 10
#define CONFIG_USE_KMALLOC	// For FSR Read DMA
#define CONFIG_ONEDRAM_READ_FILE
#define CONFIG_ONEDRAM_READ_FSR
#define CONFIG_ONEDRAM_DGSINFO
#define CONFIG_ONEDRAM_HWREV
*/

#define CONFIG_USE_KMALLOC	// For FSR Read DMA
/*--------------------------------------------------------------------------- 
 * Debug Feature
 * _DEBUG 				: printing dump message
 * _DPRAM_DEBUG_HEXDUMP	: printing dpram hexa dump log
 --------------------------------------------------------------------------*/
#undef _DEBUG
#undef _DPRAM_DEBUG_HEXDUMP

/*--------------------------------------------------------------------------- 
 * ENABLE_ERROR_DEVICE 
 --------------------------------------------------------------------------*/
#define _ENABLE_ERROR_DEVICE

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/irq.h>
#include <linux/rtc.h>
#include <linux/poll.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <mach/regs-gpio.h>
#include <mach/gpio.h>
#include <plat/gpio-cfg.h>
#include <mach/hardware.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>


#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif	/* CONFIG_PROC_FS */

#include <linux/wakelock.h>

#if defined(CONFIG_ONEDRAM_IRQPENDING)

#define eint_offset(irq)		(irq)
#define eint_irq_to_bit(irq)        (1 << (eint_offset(irq) & 0x7))

#define eint_conf_reg(irq)      ((eint_offset(irq)) >> 3)
#define eint_filt_reg(irq)      ((eint_offset(irq)) >> 2)
#define eint_mask_reg(irq)      ((eint_offset(irq)) >> 3)
#define eint_pend_reg(irq)      ((eint_offset(irq)) >> 3)
#endif	// CONFIG_ONEDRAM_IRQPENDING


#include "dpram.h"

#if defined(CONFIG_ONEDRAM_DGSINFO)
#include <linux/fsr/FSR.h>
#include <linux/fsr/FSR_BML.h>
#include <linux/fsr/FSR_LLD_4K_OneNAND.h>
#endif

#if defined(CONFIG_ONEDRAM_READ_FSR)
#include <linux/fsr/FSR.h>
#include <linux/fsr/FSR_BML.h>
#include <linux/fsr/FSR_LLD_4K_OneNAND.h>
#elif defined(CONFIG_ONEDRAM_READ_FILE)
#define MODEM_IMAGE_FILE_NAME "/data/modem/modem.bin"
#define MODEM_IMAGE_FILE_NAME_MAX 128
#define MODEM_IMAGE_BUF_SIZE_READ_UNIT 4*1024
#endif // CONFIG_ONEDRAM_READ_FSR


#ifdef CONFIG_KERNEL_DEBUG_SEC
#include <linux/kernel_sec_common.h>
#include <linux/syscalls.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <asm/uaccess.h>
#endif
#include <linux/vmalloc.h>

#define DRIVER_ID			"$Id: dpram.c, v0.01 2008/12/29 08:00:00 $"
#define DRIVER_NAME 		"DPRAM"
#define DRIVER_PROC_ENTRY	"driver/dpram"
#define DRIVER_MAJOR_NUM	252


#ifdef _DEBUG
#define dprintk(s, args...) printk("[OneDRAM] %s:%d - " s, __func__, __LINE__,  ##args)
#else
#define dprintk(s, args...)
#endif	/* _DEBUG */

#define WRITE_TO_DPRAM(dest, src, size) \
	_memcpy((void *)(DPRAM_VBASE + dest), src, size)

#define READ_FROM_DPRAM(dest, src, size) \
	_memcpy(dest, (void *)(DPRAM_VBASE + src), size)

#ifdef _ENABLE_ERROR_DEVICE
#define DPRAM_ERR_MSG_LEN			65
#define DPRAM_ERR_DEVICE			"dpramerr"
#endif	/* _ENABLE_ERROR_DEVICE */


#define IRQ_INT_ONEDRAM_AP_N		IRQ_EINT11
#define IRQ_PHONE_ACTIVE			IRQ_EINT(19)
#define MODEMOFF_TIMEOUT			30


/***************************************************************************/
static DECLARE_WAIT_QUEUE_HEAD(dpram_wait);

static int onedram_get_semaphore(const char*);
static int onedram_get_semaphore_dpram(const char*);
static int return_onedram_semaphore(const char*);
static void send_interrupt_to_phone_with_semaphore(u16 irq_mask);
static unsigned char dbl_buf[MAX_DBL_IMG_SIZE];

volatile static void __iomem *dpram_base = 0;
volatile static unsigned int *onedram_sem;
volatile static unsigned int *onedram_mailboxBA;		//send mail
volatile static unsigned int *onedram_mailboxAB;		//received mail
volatile static unsigned int *onedram_checkbitBA;		//send checkbit

static atomic_t onedram_lock;
static int onedram_lock_with_semaphore(const char*);
static void onedram_release_lock(const char*);

static void dpram_cp_dump(dump_order);
static int register_interrupt_handler(void);

static u16 check_pending_rx(void);
static void non_command_handler(u16);

static void request_semaphore_timer_func(unsigned long);
static struct timer_list request_semaphore_timer;

extern unsigned int HWREV;

static u32 cp_silentreset_count=0;
static int kerneldebuglevel=KERNEL_SEC_DEBUG_LEVEL_MID;

#ifdef CONFIG_KERNEL_DEBUG_SEC
/* CONSTANTS */
#define KERNEL_SEC_DUMP_CP_NO_AP_DUMP   1
#define KERNEL_SEC_DUMP_CP_AP_DUMP      2

#define KERNEL_SEC_DUMP_CP_UPLOAD_MAGIC_CODE   0xDEADDEAD
//#define KERNEL_SEC_DUMP_CP_DEAD_INDICATOR      0xABCD00C9
#define KERNEL_SEC_DUMP_CP_DEAD_INDICATOR      0x000000C9
#define KERNEL_SEC_DUMP_CP_MODEM_IMG_COPY_DONE 0x45674567
// "MORE" -> 0x000828A8 ; "COPY" -> 0x00
#define KERNEL_SEC_DUMP_CP_STORE_CMD_MORE       0x0ADD0ADD 
#define KERNEL_SEC_DUMP_CP_STORE_CMD_COPY_DONE  0xEDEDEDED
#define KERNEL_SEC_DUMP_CP_STORE_CMD_COPY_LAST  0xFADEFADE

#define KERNEL_SEC_DUMP_CP_SIZE1_BYTES 0xFFF800
#define KERNEL_SEC_DUMP_CP_SIZE2_BYTES 0xFFF800
#define KERNEL_SEC_DUMP_CP_SIZE3_BYTES 0x401000
//#define KERNEL_SEC_DUMP_CP_FILE_NAME "/sdcard/sd/cp-dump-%d.bin"
#define KERNEL_SEC_DUMP_CP_FILE_NAME "/sdcard/cp_onedram_dump-%d.bin"
#define KERNEL_SEC_DUMP_CP_FILE_NAME_MAX 128
#define KERNEL_SEC_DUMP_CP_BUF_SIZE_READ_UNIT 4*1024

/* LOCAL VARIABLES and FUNCIONS  */
static unsigned char cpdump_debug_file_name[DPRAM_ERR_MSG_LEN];

static int kernel_sec_dump_cp_handle1(void);
static int kernel_sec_dump_cp_handle2(void);

static int kernel_sec_dump_cp_config_uploadmode(void);

static int __volatile cp_upload_cmd_rcvd=0;

static unsigned char __volatile kernel_sec_dump_ap_cp=0; 
static void cmd_error_display_handler(void);

#endif /* CONFIG_KERNEL_DEBUG_SEC */


static int boot_complete = 0;
static int requested_semaphore = 0;
static int phone_sync = 0;
static int dump_on = 0;
static int phone_power_state = 0;
static int phone_active_state = 0;
static int modem_wait_count = 0;
static int dpram_write_pending = 0;
static int phone_shutdown = 0;
static int phone_start_ok = 0;
int receive_mms=0;
EXPORT_SYMBOL(receive_mms);

static int dpram_phone_getstatus(void);
#define DPRAM_VBASE dpram_base
static struct tty_driver *dpram_tty_driver;
static dpram_tasklet_data_t dpram_tasklet_data[MAX_INDEX];
static dpram_device_t dpram_table[MAX_INDEX] = {
	{
		.in_head_addr = DPRAM_PHONE2PDA_FORMATTED_HEAD_ADDRESS,
		.in_tail_addr = DPRAM_PHONE2PDA_FORMATTED_TAIL_ADDRESS,
		.in_buff_addr = DPRAM_PHONE2PDA_FORMATTED_BUFFER_ADDRESS,
		.in_buff_size = DPRAM_FORMATTED_BUFFER_SIZE,

		.out_head_addr = DPRAM_PDA2PHONE_FORMATTED_HEAD_ADDRESS,
		.out_tail_addr = DPRAM_PDA2PHONE_FORMATTED_TAIL_ADDRESS,
		.out_buff_addr = DPRAM_PDA2PHONE_FORMATTED_BUFFER_ADDRESS,
		.out_buff_size = DPRAM_FORMATTED_BUFFER_SIZE,
		.out_head_saved = 0,
		.out_tail_saved = 0,

		.mask_req_ack = INT_MASK_REQ_ACK_F,
		.mask_res_ack = INT_MASK_RES_ACK_F,
		.mask_send = INT_MASK_SEND_F,
	},
	{
		.in_head_addr = DPRAM_PHONE2PDA_RAW_HEAD_ADDRESS,
		.in_tail_addr = DPRAM_PHONE2PDA_RAW_TAIL_ADDRESS,
		.in_buff_addr = DPRAM_PHONE2PDA_RAW_BUFFER_ADDRESS,
		.in_buff_size = DPRAM_RAW_BUFFER_SIZE,

		.out_head_addr = DPRAM_PDA2PHONE_RAW_HEAD_ADDRESS,
		.out_tail_addr = DPRAM_PDA2PHONE_RAW_TAIL_ADDRESS,
		.out_buff_addr = DPRAM_PDA2PHONE_RAW_BUFFER_ADDRESS,
		.out_buff_size = DPRAM_RAW_BUFFER_SIZE,
		.out_head_saved = 0,
		.out_tail_saved = 0,

		.mask_req_ack = INT_MASK_REQ_ACK_R,
		.mask_res_ack = INT_MASK_RES_ACK_R,
		.mask_send = INT_MASK_SEND_R,
	},
	{
		.in_head_addr = DPRAM_PHONE2PDA_RFS_HEAD_ADDRESS,
		.in_tail_addr = DPRAM_PHONE2PDA_RFS_TAIL_ADDRESS,
		.in_buff_addr = DPRAM_PHONE2PDA_RFS_BUFFER_ADDRESS,
		.in_buff_size = DPRAM_RFS_BUFFER_SIZE,

		.out_head_addr = DPRAM_PDA2PHONE_RFS_HEAD_ADDRESS,
		.out_tail_addr = DPRAM_PDA2PHONE_RFS_TAIL_ADDRESS,
		.out_buff_addr = DPRAM_PDA2PHONE_RFS_BUFFER_ADDRESS,
		.out_buff_size = DPRAM_RFS_BUFFER_SIZE,
		.out_head_saved = 0,
		.out_tail_saved = 0,

		.mask_req_ack = INT_MASK_REQ_ACK_RFS,
		.mask_res_ack = INT_MASK_RES_ACK_RFS,
		.mask_send = INT_MASK_SEND_RFS,
	},
};

static struct tty_struct *dpram_tty[MAX_INDEX];
static struct ktermios *dpram_termios[MAX_INDEX];
static struct ktermios *dpram_termios_locked[MAX_INDEX];


#define ONEDRAM_RFS_BUFER_FREE            _IO('O', 0x01)
#define ONEDRAM_RFS_BUFER_TEST            _IO('O', 0x02)
#define SHARE_MEM_PAGE_COUNT		65
#define SHARE_MEM_SIZE			(PAGE_SIZE*SHARE_MEM_PAGE_COUNT)

typedef struct onedram_rfs_data {
	int data_in_buffer;
	int data_in_onedram;
	char * buffer;
} onedram_rfs_data_t;
static onedram_rfs_data_t onedram_rfs_data;

static void res_ack_tasklet_handler(unsigned long data);
static void send_tasklet_handler(unsigned long data);

static DECLARE_TASKLET(fmt_send_tasklet, send_tasklet_handler, 0);
static DECLARE_TASKLET(raw_send_tasklet, send_tasklet_handler, 0);
static DECLARE_TASKLET(rfs_send_tasklet, send_tasklet_handler, 0);

static DECLARE_TASKLET(fmt_res_ack_tasklet, res_ack_tasklet_handler,
		(unsigned long)&dpram_table[FORMATTED_INDEX]);
static DECLARE_TASKLET(raw_res_ack_tasklet, res_ack_tasklet_handler,
		(unsigned long)&dpram_table[RAW_INDEX]);
static DECLARE_TASKLET(rfs_res_ack_tasklet, res_ack_tasklet_handler,
		(unsigned long)&dpram_table[RFS_INDEX]);

static void semaphore_control_handler(unsigned long data);
static DECLARE_TASKLET(semaphore_control_tasklet, semaphore_control_handler, 0);

#if defined(CONFIG_ONEDRAM_DGSINFO)
/* DGS Info Cache */
static unsigned char aDGSBuf[4096];
#endif

#ifdef _ENABLE_ERROR_DEVICE
static unsigned int dpram_err_len;
static char dpram_err_buf[DPRAM_ERR_MSG_LEN];

struct class *dpram_class;

static DECLARE_WAIT_QUEUE_HEAD(dpram_err_wait_q);
static struct fasync_struct *dpram_err_async_q;
extern void usb_switch_mode(int);

#endif	/* _ENABLE_ERROR_DEVICE */

static DECLARE_MUTEX(write_mutex);
struct wake_lock dpram_wake_lock;

/* tty related functions. */
static inline void byte_align(unsigned long dest, unsigned long src)
{
	u16 *p_src;
	volatile u16 *p_dest;

	if (!(dest % 2) && !(src % 2)) {
		p_dest = (u16 *)dest;
		p_src = (u16 *)src;

		*p_dest = (*p_dest & (u16)0xFF00) | (*p_src & (u16)0x00FF);
	}

	else if ((dest % 2) && (src % 2)) {
		p_dest = (u16 *)(dest - 1);
		p_src = (u16 *)(src - 1);

		*p_dest = (*p_dest & (u16)0x00FF) | (*p_src & (u16)0xFF00);
	}

	else if (!(dest % 2) && (src % 2)) {
		p_dest = (u16 *)dest;
		p_src = (u16 *)(src - 1);

		*p_dest = (u16)((u16)(*p_dest & (u16)0xFF00) | (u16)((*p_src >> 8) & (u16)0x00FF));
	}

	else if ((dest % 2) && !(src % 2)) {
		p_dest = (u16 *)(dest - 1);
		p_src = (u16 *)src;

		*p_dest = (u16)((u16)(*p_dest & (u16)0x00FF) | (u16)((*p_src << 8) & (u16)0xFF00));
	}

	else {
		dprintk("oops.~\n");
	}
}

static inline void _memcpy(void *p_dest, const void *p_src, int size)
{
	int i;
	unsigned long dest = (unsigned long)p_dest;
	unsigned long src = (unsigned long)p_src;

	if (*onedram_sem != 0x1) {
		printk("[OneDRAM] memory access without semaphore!: %d\n", *onedram_sem);
		return;
	}
	if (size <= 0) {
		return;
	}

	if(!(size % 2) && !(dest % 2) && !(src % 2)) {
		for(i = 0; i < (size/2); i++)
			*(((u16 *)dest) + i) = *(((u16 *)src) + i);
	}
	else {
		for(i = 0; i < size; i++)
			byte_align(dest+i, src+i);
	}
}

static inline int _memcmp(u16 *dest, u16 *src, int size)
{
	int i = 0;

	if (*onedram_sem != 0x1) {
		printk("[OneDRAM] (%s) memory access without semaphore!: %d\n", __func__, *onedram_sem);
		return 1;
	}

	size =size >> 1;

	while (i < size) {
		if (*(dest + i) != *(src + i)) {
			return 1;
		}
		i++ ;
	}

	return 0;
}


static void send_interrupt_to_phone(u16 irq_mask)
{
#if defined(CONFIG_ONEDRAM_CHECKBIT)
	int retry = 50;
	while (retry > 0 && (*onedram_checkbitBA&0x1) ) {
		retry--;
		mdelay(2);
	}
#endif /* CONFIG_ONEDRAM_CHECKBIT */

	dprintk("=====>%s: irq_mask: %x\n", __func__, irq_mask);
	*onedram_mailboxBA = irq_mask;
}

#ifdef _DPRAM_DEBUG_HEXDUMP
#define isprint(c)	((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
void hexdump(const char *buf, int len)
{
	char str[80], octet[10];
	int ofs, i, l;

	for (ofs = 0; ofs < len; ofs += 16) {
		sprintf( str, "%03d: ", ofs );

		for (i = 0; i < 16; i++) {
			if ((i + ofs) < len)
				sprintf( octet, "%02x ", buf[ofs + i] );
			else
				strcpy( octet, "   " );

			strcat( str, octet );
		}
			strcat( str, "  " );
			l = strlen( str );

		for (i = 0; (i < 16) && ((i + ofs) < len); i++)
			str[l++] = isprint( buf[ofs + i] ) ? buf[ofs + i] : '.';

		str[l] = '\0';
		printk( "%s\n", str );
	}
}

static void print_debug_current_time(void)
{
	struct rtc_time tm;
	struct timeval time;

	/* get current time */
	do_gettimeofday(&time);

	/* set current time */
	rtc_time_to_tm(time.tv_sec, &tm);

	printk(KERN_DEBUG "%02d-%02d %02d:%02d:%02d.%ld \n", tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, time.tv_usec);
}
#endif

#define yisprint(c)	((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
void yhexdump(const char *buf, int len)
{
	char str[80], octet[10];
	int ofs, i, l;

	printk("<yhexdump()> : ADDR - [0x%08x], len -[%d]\n", (unsigned int)buf, len);
	for (ofs = 0; ofs < len; ofs += 16) {
		sprintf( str, "%03d: ", ofs );

		for (i = 0; i < 16; i++) {
			if ((i + ofs) < len)
				sprintf( octet, "%02x ", buf[ofs + i] );
			else
				strcpy( octet, "   " );

			strcat( str, octet );
		}
			strcat( str, "  " );
			l = strlen( str );

		for (i = 0; (i < 16) && ((i + ofs) < len); i++)
			str[l++] = yisprint( buf[ofs + i] ) ? buf[ofs + i] : '.';

		str[l] = '\0';
		printk( "%s\n", str );
	}
}

EXPORT_SYMBOL(yhexdump);


char	multipdp_rbuf[128 * 1024];
static int dpram_write(dpram_device_t *device, const unsigned char *buf, int len);
static int  (*multipdp_rx_noti_func)(char *, int);
static inline int dpram_tty_insert_data(dpram_device_t *device, const u8 *psrc, u16 size);

int  multipdp_buf_copy(int index, char *dpram, int size)
{
	if( index < 0 || index > sizeof(multipdp_rbuf) || (index + size) > sizeof(multipdp_rbuf))
		return -1;

	//printk("multipdp_buf_copy:index=%d size=%d\n", index, size);
	memcpy( (void *)&multipdp_rbuf[index], (void *)dpram, size);
	return( size);

}


EXPORT_SYMBOL(multipdp_buf_copy);


int	multipdp_rx_noti_regi( int (*rx_cfunc)(char *, int))
{
	multipdp_rx_noti_func =  rx_cfunc;
}
EXPORT_SYMBOL(multipdp_rx_noti_regi);
extern	int multipdp_rx_datalen = 0;

int	multipdp_rx_data(dpram_device_t *device, int len)
{
	static int inuse_flag = 0;
	
	int ret = 0;	
	
	if( len == 0 )
		return 0;
		
	if( inuse_flag )
		printk("***** inuse_flag = %d\n", inuse_flag);	
		
	inuse_flag ++;
	
	//yhexdump(multipdp_rbuf, len);	
	//multipdp_rbuf
	if( multipdp_rx_noti_func)
	{
		//printk("multipdp_rx_data Before(noti_func) : len=%d\n",len);
		multipdp_rx_datalen = len;

		ret = multipdp_rx_noti_func(multipdp_rbuf, len);
		//memset(multipdp_rbuf, 0x00, len);
		//printk("multipdp_rx_data After(noti_func) : ret=%d\n",ret);
	}

	inuse_flag --;
	
	return(ret);
}

int	multipdp_dump(void)
{
	yhexdump(multipdp_rbuf, multipdp_rx_datalen);	
}
EXPORT_SYMBOL(multipdp_dump);



int multipdp_write(const unsigned char *buf, int len)
{
	int i, ret;
	// FORMATTED_INDEX : dpram0, RAW_INDEX : dpram1
	dpram_device_t *device = &dpram_table[RAW_INDEX];

	for(i =0; i< CONFIG_ONEDRAM_TX_RETRY; i++)
	{
		ret = dpram_write(device, buf, len);
		if( ret > 0 )
		{
			break;
		}
		printk(KERN_DEBUG "dpram_write() failed: %d, i(%d)\n", ret, i);
	}
	if ( i>= CONFIG_ONEDRAM_TX_RETRY) {
		printk(KERN_DEBUG "dpram_write() failed: %d\n", ret);
	}
	
	return ret;
}
EXPORT_SYMBOL(multipdp_write);

static int dpram_write(dpram_device_t *device,	const unsigned char *buf, int len)
{
	int retval = 0;
	int size = 0;

	u32 freesize = 0;
	u32 next_head = 0;
	
	u32 head, tail;
	u16 irq_mask = 0;
	
#ifdef _DPRAM_DEBUG_HEXDUMP
	printk("\n\n#######[dpram write : head - %04d, tail - %04d]######\n", head, tail);
	hexdump(buf, len);
#endif

	if(!onedram_get_semaphore_dpram(__func__)) 
	{
		printk(KERN_DEBUG "%s%04d : %d len[%d]\n", __func__, __LINE__, *onedram_sem, len);
		return -EAGAIN;
	}
		
	if(onedram_lock_with_semaphore(__func__) < 0)
	{
		printk(KERN_DEBUG "%s%04d : %d len[%d]\n", __func__, __LINE__, *onedram_sem, len);
		return -EAGAIN;
	}

	READ_FROM_DPRAM(&head, device->out_head_addr, sizeof(head));
	READ_FROM_DPRAM(&tail, device->out_tail_addr, sizeof(tail));

	if(head < tail)
		freesize = tail - head - 1;
	else
		freesize = device->out_buff_size - head + tail -1;

	if(freesize >= len){

		next_head = head + len;

		if(next_head < device->out_buff_size) {
			size = len;
			WRITE_TO_DPRAM(device->out_buff_addr + head, buf, size);
			retval = size;
		}
		else {
			next_head -= device->out_buff_size;

			size = device->out_buff_size - head;
			WRITE_TO_DPRAM(device->out_buff_addr + head, buf, size);
			retval = size;

			size = next_head;
			WRITE_TO_DPRAM(device->out_buff_addr, buf + retval, size);
			retval += size;
		}

		head = next_head;

		WRITE_TO_DPRAM(device->out_head_addr, &head, sizeof(head));
				
	}
	irq_mask = INT_MASK_VALID | device->mask_send;

       	onedram_release_lock(__func__);
       	send_interrupt_to_phone_with_semaphore(irq_mask);

	if (retval <= 0) {
		printk("dpram_write : not enough space Fail (-1): freesize[%d],len[%d]\n",freesize, len);
		device->out_head_saved = head;
		device->out_tail_saved = tail;
		return -EAGAIN;
	}
    // Can access head & tail info without smp.
	device->out_head_saved = head;
	device->out_tail_saved = tail;

#ifdef _DPRAM_DEBUG_HEXDUMP
	printk("#######[dpram write : head - %04d, tail - %04d]######\n\n", head, tail);
	print_debug_current_time();
#endif

	return retval;
	
}

static inline
int dpram_tty_insert_data(dpram_device_t *device, const u8 *psrc, u16 size)
{
#define CLUSTER_SEGMENT 1550
	u16 copied_size = 0;
	int retval = 0;
	// ... ..... multipdp. .... raw data. ....
	if (size > CLUSTER_SEGMENT){
		while (size) {
			copied_size = (size > CLUSTER_SEGMENT) ? CLUSTER_SEGMENT : size;
			tty_insert_flip_string(device->serial.tty, psrc + retval, copied_size);

			size = size - copied_size;
			retval += copied_size;
		}

		return retval;
	}

	return tty_insert_flip_string(device->serial.tty, psrc, size);
}

static int dpram_read(dpram_device_t *device, const u16 non_cmd)
{
	int retval = 0;
	int size = 0;
	u32 head, tail, up_tail;

	struct tty_struct *tty = device->serial.tty;
	if(!onedram_get_semaphore_dpram(__func__)) 
	{
		printk(KERN_DEBUG "%s%04d : %d \n", __func__, __LINE__, *onedram_sem);
		return -EAGAIN;
	}

	if(onedram_lock_with_semaphore(__func__) < 0)
	{
		printk(KERN_DEBUG "%s%04d : %d\n", __func__, __LINE__, *onedram_sem);
		return -EAGAIN;
	}
	
	READ_FROM_DPRAM(&head, device->in_head_addr, sizeof(head));
	READ_FROM_DPRAM(&tail, device->in_tail_addr, sizeof(tail));

#ifdef _DPRAM_DEBUG_HEXDUMP
	printk("\n\n#######[dpram read : head - %04d, tail - %04d]######\n", head, tail);
#endif
	//printk("===index = %d, head=[%d], tail=[%d]\n", tty->index, head, tail);

	if (head != tail) {
		up_tail = 0;
		//printk("===size=[%d]\n",  head > tail ? (head - tail):(device->in_buff_size - tail + head));
		// ------- tail ++++++++++++ head -------- //
		if (head > tail) {
			size = head - tail;
      		if( tty->index != 1)	//index : 0=dpram0, 1=dpram1, 2=rfs	
			retval = dpram_tty_insert_data(device, (unsigned char *)(DPRAM_VBASE + (device->in_buff_addr + tail)), size);
		else	//2: dpram1
		    	retval = multipdp_buf_copy( 0, (char *)(DPRAM_VBASE + (device->in_buff_addr + tail)), size);

		    if (retval != size)
				dprintk("Size Mismatch : Real Size = %d, Returned Size = %d\n", size, retval);

#ifdef _DPRAM_DEBUG_HEXDUMP
			hexdump((unsigned char *)(DPRAM_VBASE + (device->in_buff_addr + tail)), size);
#endif
		}

		// +++++++ head ------------ tail ++++++++ //
		else {
			int tmp_size = 0;

			// Total Size.
			size = device->in_buff_size - tail + head;

			// 1. tail -> buffer end.
			tmp_size = device->in_buff_size - tail;
      			if( tty->index != 1)	//index : 0=dpram0, 1=dpram1, 2=rfs	
			retval = dpram_tty_insert_data(device, (unsigned char *)(DPRAM_VBASE + (device->in_buff_addr + tail)), tmp_size);
			else
		    		retval = multipdp_buf_copy( 0, (char *)(DPRAM_VBASE + (device->in_buff_addr + tail)), tmp_size);

			if (retval != tmp_size)
				dprintk("Size Mismatch : Real Size = %d, Returned Size = %d\n", tmp_size, retval);
			
#ifdef _DPRAM_DEBUG_HEXDUMP
			hexdump((unsigned char *)(DPRAM_VBASE + (device->in_buff_addr + tail)), tmp_size);
#endif
			// 2. buffer start -> head.
			if (size > tmp_size) {
      			if( tty->index != 1)	//index : 0=dpram0, 1=dpram1, 2=rfs	
				dpram_tty_insert_data(device, (unsigned char *)(DPRAM_VBASE + device->in_buff_addr), size - tmp_size);
			else
		       		multipdp_buf_copy( tmp_size, (char *)(DPRAM_VBASE + device->in_buff_addr), size - tmp_size);
				
#ifdef _DPRAM_DEBUG_HEXDUMP
				hexdump((unsigned char *)(DPRAM_VBASE + device->in_buff_addr), size - tmp_size);
#endif
				retval += (size - tmp_size);
			}
		}

		/* new tail */
		up_tail = (u32)((tail + retval) % device->in_buff_size);
		WRITE_TO_DPRAM(device->in_tail_addr, &up_tail, sizeof(up_tail));
	}
	
	device->out_head_saved = head;
	device->out_tail_saved = tail;

#ifdef _DPRAM_DEBUG_HEXDUMP
	printk("#######[dpram read : head - %04d, tail - %04d]######\n\n", head, up_tail);
	print_debug_current_time();
#endif

	onedram_release_lock(__func__);
	if (non_cmd & device->mask_req_ack)
		send_interrupt_to_phone_with_semaphore(INT_NON_COMMAND(device->mask_res_ack));

	if( tty->index == 1)
		multipdp_rx_data(device, retval);
	return retval;
	
}

static int dpram_read_rfs(dpram_device_t *device, const u16 non_cmd)
{
	int retval = 0;
	int size = 0;
	u32 head, tail, up_tail;

	if(!onedram_get_semaphore(__func__)) 
		return -EAGAIN;

	if(onedram_lock_with_semaphore(__func__) < 0)
		return -EAGAIN;
	READ_FROM_DPRAM(&head, device->in_head_addr, sizeof(head));
	READ_FROM_DPRAM(&tail, device->in_tail_addr, sizeof(tail));

#ifdef _DPRAM_DEBUG_HEXDUMP
	printk("\n\n#######[dpram read : head - %04d, tail - %04d]######\n", head, tail);
#endif

	if (head != tail) {
		up_tail = 0;

		// ------- tail ++++++++++++ head -------- //
		if (head > tail) {
			size = head - tail;
			if(onedram_rfs_data.data_in_buffer)
			{
				dprintk("rfs_buffer is not free = %d\n", onedram_rfs_data.data_in_buffer);
				onedram_rfs_data.data_in_onedram = 1;
        		onedram_release_lock(__func__);
				return 0;
			}
			else
			{
				onedram_rfs_data.data_in_onedram = 0;
				memcpy(onedram_rfs_data.buffer, (unsigned char *)(DPRAM_VBASE + (device->in_buff_addr + tail)), size);
				onedram_rfs_data.data_in_buffer = 1;
				retval = dpram_tty_insert_data(device,(unsigned char *)&size, sizeof(size));
			       if (retval != sizeof(size))
				    dprintk("Size Mismatch : Real Size = %d, Returned Size = %d\n", size, retval);
				retval = size;
			}
			

#ifdef _DPRAM_DEBUG_HEXDUMP
			hexdump((unsigned char *)(DPRAM_VBASE + (device->in_buff_addr + tail)), size);
#endif
		}

		// +++++++ head ------------ tail ++++++++ //
		else {
			int tmp_size = 0;

			// Total Size.
			size = device->in_buff_size - tail + head;

			// 1. tail -> buffer end.
			tmp_size = device->in_buff_size - tail;
			//retval = dpram_tty_insert_data(device, (unsigned char *)(DPRAM_VBASE + (device->in_buff_addr + tail)), tmp_size);

			if(onedram_rfs_data.data_in_buffer)
			{
				dprintk("rfs_buffer is not free = %d\n", onedram_rfs_data.data_in_buffer);
				onedram_rfs_data.data_in_onedram = 1;
        		onedram_release_lock(__func__);
				return 0;
			}
			else
			{
				onedram_rfs_data.data_in_onedram = 0;
				memcpy(onedram_rfs_data.buffer, (unsigned char *)(DPRAM_VBASE + (device->in_buff_addr + tail)), tmp_size);
				memcpy((onedram_rfs_data.buffer+tmp_size), (unsigned char *)(DPRAM_VBASE + (device->in_buff_addr)), size - tmp_size);
				onedram_rfs_data.data_in_buffer = 1;
				dpram_tty_insert_data(device, (unsigned char *)&size, sizeof(size));
				retval = size;
			       if (retval != sizeof(size))
				    dprintk("Size Mismatch : Real Size = %d, Returned Size = %d\n", size, retval);
				retval = size;

			}

#ifdef _DPRAM_DEBUG_HEXDUMP
			hexdump((unsigned char *)(DPRAM_VBASE + (device->in_buff_addr + tail)), tmp_size);
#endif
			// 2. buffer start -> head.
			//if (size > tmp_size) {
			//	dpram_tty_insert_data(device, (unsigned char *)(DPRAM_VBASE + device->in_buff_addr), size - tmp_size);
				
			//	retval += (size - tmp_size);
			//}
		}

		/* new tail */
		up_tail = (u32)((tail + retval) % device->in_buff_size);
		WRITE_TO_DPRAM(device->in_tail_addr, &up_tail, sizeof(up_tail));
	}
	
	device->out_head_saved = head;
	device->out_tail_saved = tail;

#ifdef _DPRAM_DEBUG_HEXDUMP
	printk("#######[dpram read : head - %04d, tail - %04d]######\n\n", head, up_tail);
	print_debug_current_time();
#endif

	onedram_release_lock(__func__);
	if (non_cmd & device->mask_req_ack)
		send_interrupt_to_phone_with_semaphore(INT_NON_COMMAND(device->mask_res_ack));

	return 0;
	
}

static int onedram_get_semaphore(const char *func)
{
	int i, retry = 60;
	const u16 cmd = INT_COMMAND(INT_MASK_CMD_SMP_REQ);
	if(dump_on) return 0;

	for(i = 0; i < retry; i++) {
		if(*onedram_sem == 0x1) return 1;
		dprintk("=====>%s: irq_mask: %x\n", __func__, cmd);
		*onedram_mailboxBA = cmd;
		mdelay(5);
	}

	dprintk("Failed to get a Semaphore (%s) sem:%d, phone status: %s\n", func, *onedram_sem,	(dpram_phone_getstatus() ? "ACTIVE" : "INACTIVE"));

	return 0;
}

static int onedram_get_semaphore_dpram(const char *func)
{
	int i, retry = 200;
	const u16 cmd = INT_COMMAND(INT_MASK_CMD_SMP_REQ);
	if(dump_on) return 0;

	if (*onedram_sem == 1)
		return 1;

	*onedram_mailboxBA = cmd;

	for(i = 1; i < retry; i++) {
		if(*onedram_sem == 0x1)	return 1;
		if(i%10 == 0) *onedram_mailboxBA = cmd;
		//printk("sem: %x\n", cmd);
		udelay(200);
	}

	dprintk("Failed to get a Semaphore (%s) sem:%d, phone status: %s\n", func, *onedram_sem,	(dpram_phone_getstatus() ? "ACTIVE" : "INACTIVE"));

	return 0;
}

static void send_interrupt_to_phone_with_semaphore(u16 irq_mask)
{
	if(dump_on) return;
	if(!atomic_read(&onedram_lock)) 
	{
	#if defined(CONFIG_ONEDRAM_CHECKBIT)
	int retry = 50;
	while (retry > 0 && (*onedram_checkbitBA&0x1) ) {
		retry--;
		mdelay(2);
	}
	#endif /* CONFIG_ONEDRAM_CHECKBIT */
	
	
		if(*onedram_sem == 0x1) { 	
			*onedram_sem = 0x0;
			dprintk("=====>%s: irq_mask: %x with sem\n", __func__, irq_mask);
			*onedram_mailboxBA = irq_mask;
			requested_semaphore = 0;
			del_timer(&request_semaphore_timer);
		}else {
			dprintk("=====>%s: irq_mask: %x already cp sem\n", __func__, irq_mask);
			*onedram_mailboxBA = irq_mask;
		}
	}else {
		dprintk("lock set. can't return semaphore.\n", __func__);
	}
		

}

static int return_onedram_semaphore(const char* func)
{

	if(!atomic_read(&onedram_lock)) 
	{
		if(*onedram_sem == 0x1) { 	
			*onedram_sem = 0x0;
		}
		requested_semaphore = 0;
		del_timer(&request_semaphore_timer);

		dprintk("%s %d\n", __func__, __LINE__);
		return 1;
	}else {
		mod_timer(&request_semaphore_timer,  jiffies + HZ/2);
		requested_semaphore++;
		dprintk("%s %d\n", __func__, __LINE__);
		return 0;
	}
}

static int onedram_lock_with_semaphore(const char* func)
{
	int lock_value;

	if(!(lock_value = atomic_inc_return(&onedram_lock)))
		dprintk("(lock) fail to locking onedram access. %d\n", func, lock_value);

	if(lock_value != 1)
		dprintk("(lock) lock_value: %d\n", func, lock_value);

	if(*onedram_sem == 0x1) 
		return 0;	
	else {
		dprintk("(lock) failed.. no sem\n", func);
		if((lock_value = atomic_dec_return(&onedram_lock)) < 0)
			dprintk("(lock) fail to unlocking onedram access. %d\n", func, lock_value);

		if(lock_value != 0)
			dprintk("(lock) lock_value: %d\n", func, lock_value);
		return -1;
	}
}

static void onedram_release_lock(const char* func)
{
	int lock_value;

	if((lock_value = atomic_dec_return(&onedram_lock)) < 0)
		dprintk("(release) fail to unlocking onedram access. %d\n", func, lock_value);

	if(requested_semaphore) {
		if(!atomic_read(&onedram_lock)) {
			if(*onedram_sem == 0x1) { 	
				dprintk("(release) requested semaphore(%d) return to Phone.\n", func, requested_semaphore);
				*onedram_sem = 0x0;
				requested_semaphore = 0;
				del_timer(&request_semaphore_timer);
			}
		}
	}

	if(lock_value != 0)
		dprintk("(release) lock_value: %d\n", func, lock_value);

}

static int dpram_shared_bank_remap(void)
{
	// kt.hur
	//dpram_base =(void __iomem *) ONEDRAM1G_SHARED_AREA_VIRT;
	dpram_base = ioremap_nocache(DPRAM_START_ADDRESS_PHYS + DPRAM_SHARED_BANK, DPRAM_SHARED_BANK_SIZE);
	if (dpram_base == NULL) {
		printk("failed ioremap\n");
		return -ENOENT;
		}
		
	onedram_sem = DPRAM_VBASE + DPRAM_SMP; 
	onedram_mailboxBA = DPRAM_VBASE + DPRAM_MBX_BA;
	onedram_mailboxAB = DPRAM_VBASE + DPRAM_MBX_AB;
	onedram_checkbitBA = DPRAM_VBASE + ONEDRAM_CHECK_BA;
	atomic_set(&onedram_lock, 0);
	return 0;
}
static int ReadPhoneFile(unsigned char *PsiBuffer, 	unsigned char *ImageBuffer, 
						unsigned long Psi_Length, unsigned long Total_length,
						int bml_part_no )
{
// replace it to file read
#if defined(CONFIG_ONEDRAM_READ_FILE)
	// open file for modem image
	static char modem_filename[MODEM_IMAGE_FILE_NAME_MAX] = "/dev/block/bml12"; //data/modem/modem.bin";
	int buf_size = MODEM_IMAGE_BUF_SIZE_READ_UNIT;
	int dpram_offset=0x0;
	int bytes_remained=0;
	int ret = 0;
	struct file *file_p=NULL;
	int old_fs = get_fs();
	loff_t offset_t = 0;
	void *pCpData = NULL;
	
	bytes_remained = Total_length - Psi_Length - MAX_DEFAULT_NV_SIZE;

	set_fs(KERNEL_DS);

	file_p = filp_open((const char*)modem_filename, O_RDONLY , 00644);

	if (IS_ERR(file_p))
	{
	    printk("\n %s open failed(%d)\n",modem_filename, ERR_PTR(file_p));
	    file_p = NULL;
	    set_fs(old_fs);    
	    return file_p;
	}

	pCpData = kmalloc(buf_size,GFP_KERNEL);
	if (pCpData == NULL) {
		printk("[%s]fail to allocate cp buffer for modem image\n",__func__);
		return -1;
	}
	
	// read modem image to onedram base

	offset_t = file_p->f_op->llseek(file_p, MAX_DBL_IMG_SIZE, SEEK_SET);

	while(bytes_remained > 0) {
		if (bytes_remained < buf_size) {
			ret = file_p->f_op->read(file_p, (void*)(pCpData), bytes_remained, &file_p->f_pos);
			memcpy((void*)(dpram_base + dpram_offset), (void *)pCpData, bytes_remained);
			// TODO :
			// error handling
			break;
		} else {
			ret = file_p->f_op->read(file_p, (void*)(pCpData), buf_size, &file_p->f_pos);
			memcpy((void*)(dpram_base + dpram_offset), (void *)pCpData, buf_size);
			// TODO :
			// error handling
		}
		bytes_remained = bytes_remained - buf_size;
		dpram_offset = dpram_offset + buf_size;
	}

#if 0		//print buf
	int i;

	printk("\n[MODEM image data : offset 0x100000]\n");
	char * dataptr =(char*)(dpram_base + 0x100000);
	for (i=0 ; i < 512 ; i++) {
		printk("%02x  ",*(dataptr + i));
		if ((i+1)%16 == 0) printk("\n");
	}
#endif

	if(pCpData)
		kfree(pCpData);
	

	filp_close(file_p,NULL);

	set_fs(old_fs); 

	return 0;
#else
    INT32           nRet;
    INT32           nPAMRe;
    UINT32          nVol;
    UINT32          n1stVpn;
    UINT32          nNumOfPgs;
    UINT32          nPgsPerUnit;
    FSRPartI        stPartI;
    FSRPartEntry   *pPEntry;
    FsrVolParm      stFsrVolParm[FSR_MAX_VOLS];

	unsigned long   modem_page_num = 0;
	unsigned long   dbl_page_num = 0;
	unsigned long   default_nv_page_num = 0;

	nVol = 0; 
    
	nPAMRe = FSR_PAM_GetPAParm(stFsrVolParm);
	if (nPAMRe != FSR_PAM_SUCCESS)
	{
		FSR_DBZ_RTLMOUT(FSR_DBZ_ERROR, (TEXT("[BM :ERR]   FSR_PAM_GetPAParm() is failed! / nPAMRe=0x%x / %s,line:%d\r\n"), nPAMRe, __FSR_FUNC__, __LINE__));
		return -1;
	}
	
	/* if the current volume isn't available, skip test  */
	if (stFsrVolParm[nVol].nDevsInVol == 0)
	{
		printk("=====>[%s,%d] nVol: %d\n", __func__, __LINE__, nVol);
		return -1;
	}

	/* call BML_Open */
	nRet = FSR_BML_Open(nVol, FSR_BML_FLAG_NONE);

	if (nRet == FSR_BML_PAM_ACCESS_ERROR)
	{
		FSR_DBZ_RTLMOUT(FSR_DBZ_WARN, (TEXT("[BM :ERR]   FSR_BML_Open Error\r\n")));
		FSR_DBZ_RTLMOUT(FSR_DBZ_WARN, (TEXT("[BM :ERR]   However, if you don't use vol#%d,No Problem!\r\n"), nVol));

        return -1;
	}
    else if (nRet != FSR_BML_SUCCESS) 
	{
		FSR_DBZ_RTLMOUT(FSR_DBZ_ERROR, (TEXT("[BM :ERR]   FSR_BML_Open Error\r\n")));
        while(1);
	}

	/* call BML_GetFullPartI for get partition information about Number of Partition entry */
	nRet = FSR_BML_GetFullPartI(nVol, &stPartI);
	if (nRet != FSR_BML_SUCCESS)
	{
		FSR_DBZ_RTLMOUT(FSR_DBZ_ERROR, (TEXT("[BM :ERR]   FSR_BML_IOCtl Error\r\n")));
        while(1);
	}
		/* Get structure of part entry */
	pPEntry = &stPartI.stPEntry[11];

	/* Get partition entry */
	if (FSR_BML_GetVirUnitInfo(nVol, pPEntry->n1stVun, &n1stVpn, &nPgsPerUnit) != FSR_BML_SUCCESS)
	{
		FSR_DBZ_RTLMOUT(FSR_DBZ_ERROR, (TEXT("[BM :ERR]   FSR_BML_GetVirUnitInfo Error\r\n")));
		while(1);
	}

	nNumOfPgs     = nPgsPerUnit * pPEntry->nNumOfUnits;
	
	dprintk("  - pPEntry->n1stVun     : %d\n", pPEntry->n1stVun);
	dprintk("  - n1stVpn              : %d\n", n1stVpn);
	dprintk("  - modeamPgNum          : %d\n", n1stVpn + 5);
	dprintk("  - pPEntry->nNumOfUnits : %d\n", pPEntry->nNumOfUnits);
	dprintk("  - nPgsPerUnit          : %d\n", nPgsPerUnit);
	dprintk("  - pPEntry->nNumOfPgs   : %d\n", nNumOfPgs);

	dbl_page_num = Psi_Length / 4096;
	default_nv_page_num = MAX_DEFAULT_NV_SIZE / 4096;
	modem_page_num = (Total_length /4096) - dbl_page_num;

	// load os image except psi & default nv
	dprintk("  - dbl_page_num: 0x%x, modem_page_num: 0x%x\n", dbl_page_num, modem_page_num);

#ifdef CONFIG_USE_KMALLOC	
	/*
	 * Modified for FSR Read DMA
	 * Use kmalloc buffer to read data by using FSR_BML_Read
 	 */
 	int nPageCnt, nCopyCnt=1000;
	if(cp_silentreset_count)
	{
		printk(KERN_DEBUG "silent reset %d use 4k kmalloc \n",cp_silentreset_count);
		nCopyCnt=1;
	}
	unsigned char *temp = kmalloc(4096*nCopyCnt, GFP_KERNEL);

	for( nPageCnt = 0 ; nPageCnt < modem_page_num; nPageCnt+=nCopyCnt)
	{
        if (nPageCnt + nCopyCnt >= modem_page_num)
            nCopyCnt = nCopyCnt - (nPageCnt + nCopyCnt - modem_page_num);
        
		nRet = FSR_BML_Read(nVol,                                   /* Volume Number                         */
				n1stVpn + dbl_page_num + nPageCnt,                        /* First virtual page number for reading */
				nCopyCnt,
				temp,
				NULL,                                   /* pSBuf                                 */
				FSR_BML_FLAG_ECC_ON);                   /* nFlag                                 */

		if (nRet != FSR_BML_SUCCESS)
		{
			FSR_DBZ_RTLMOUT(FSR_DBZ_ERROR, (TEXT("[BM :ERR]   FSR_BML_Read Error\r\n")));
			while(1);
		}
		//printk("copy page (%d)\n",nPageCnt);
		_memcpy(ImageBuffer, temp, 4096*nCopyCnt);

		ImageBuffer += (4096*nCopyCnt);
	}

	kfree(temp);
#endif

	if (nRet != FSR_BML_SUCCESS)
	{
		FSR_DBZ_RTLMOUT(FSR_DBZ_ERROR, (TEXT("[BM :ERR]   FSR_BML_Read Error\r\n")));
		while(1);
	}
	
	dprintk(" +---------------------------------------------+\n");
	dprintk(" |          complete phone image load          |\n");
	dprintk(" +---------------------------------------------+\n");
#if 0
	int i;
	dprintk("  - First 16 byte: ");
	for(i =0; i< 16; i++) {
		printk("%02x", ImageBuffer[i]);
		if((i%2)) printk(" ");
	}
#endif
#endif
	printk(KERN_DEBUG "PI\n");
}

static void dpram_clear(void)
{
	long i = 0;
	unsigned long flags;
	
	u16 value = 0;

	/* @LDK@ clear DPRAM except interrupt area */
	local_irq_save(flags);

	for (i = DPRAM_PDA2PHONE_FORMATTED_HEAD_ADDRESS;
			i < DPRAM_SIZE - (DPRAM_INTERRUPT_PORT_SIZE * 2);
			i += 2)
	{
		*((u16 *)(DPRAM_VBASE + i)) = 0;
	}

	local_irq_restore(flags);

	value = *onedram_mailboxAB;
}

static int dpram_init_and_report(void)
{
	const u16 magic_code = 0x00aa;
	const u16 init_start = INT_COMMAND(INT_MASK_CMD_INIT_START);
	const u16 init_end = INT_COMMAND(INT_MASK_CMD_INIT_END|INT_MASK_CP_AIRPLANE_BOOT|INT_MASK_CP_AP_ANDROID);
	u16 ac_code = 0;

	if (*onedram_sem != 0x1) {
		printk("[OneDRAM] %s semaphore: %d\n", __func__, *onedram_sem);
		if(!onedram_get_semaphore(__func__)) {
			printk("[OneDRAM] %s failed to onedram init!!! semaphore: %d\n", __func__, *onedram_sem);
			return -1;
		}
	}

	/* @LDK@ send init start code to phone */
	if(onedram_lock_with_semaphore(__func__) < 0)
		return -1;

	/* @LDK@ write DPRAM disable code */
	WRITE_TO_DPRAM(DPRAM_ACCESS_ENABLE_ADDRESS, &ac_code, sizeof(ac_code));

	/* @LDK@ dpram clear */
	dpram_clear();

	/* @LDK@ write magic code */
	WRITE_TO_DPRAM(DPRAM_MAGIC_CODE_ADDRESS,
			&magic_code, sizeof(magic_code));

	/* @LDK@ write DPRAM enable code */
	ac_code = 0x0001;
	WRITE_TO_DPRAM(DPRAM_ACCESS_ENABLE_ADDRESS, &ac_code, sizeof(ac_code));

	/* @LDK@ send init end code to phone */
	onedram_release_lock(__func__);
	send_interrupt_to_phone_with_semaphore(init_end);
	dprintk("[OneDRAM] Send to MailboxBA 0x%x (onedram init finish).\n", init_end);

	phone_sync = 1;
	return 0;
}

static void dpram_drop_data(dpram_device_t *device)
{
	u32 head;


	if(*onedram_sem == 0x1) {
	READ_FROM_DPRAM(&head, device->in_head_addr, sizeof(head));
	WRITE_TO_DPRAM(device->in_tail_addr, &head, sizeof(head));
	}
}

static int dpram_phone_image_load(int bml_part_no)
{
	static int count=0;

    if (HWREV<=3)
    {
    	gpio_set_value(GPIO_USIM_BOOT, GPIO_LEVEL_LOW); 
        mdelay(50);
    	gpio_set_value(GPIO_FLM_SEL_R03, GPIO_LEVEL_LOW);
    	
    	dprintk("  - GPIO_FLM_SEL : %s, GPIO_USIM_BOOT : %s\n", 
    	gpio_get_value(GPIO_FLM_SEL_R03)?"HIGH":"LOW", gpio_get_value(GPIO_USIM_BOOT)?"HIGH":"LOW");
    }
    else
    {
    	gpio_set_value(GPIO_USIM_BOOT, GPIO_LEVEL_LOW); 
        mdelay(50);
    	gpio_set_value(GPIO_FLM_SEL_R04, GPIO_LEVEL_LOW);
    	
    	dprintk("  - GPIO_FLM_SEL : %s, GPIO_USIM_BOOT : %s\n", 
    	gpio_get_value(GPIO_FLM_SEL_R04)?"HIGH":"LOW", gpio_get_value(GPIO_USIM_BOOT)?"HIGH":"LOW");
    }
	
	//dprintk("  - GPIO_FLM_SEL : %s, GPIO_USIM_BOOT : %s\n", 
	//gpio_get_value(GPIO_FLM_SEL)?"HIGH":"LOW", gpio_get_value(GPIO_USIM_BOOT)?"HIGH":"LOW");

	dprintk(" +---------------------------------------------+\n");
	dprintk(" |   CHECK DBL DOWNLOAD  &  LOAD PHONE IMAGE   |\n");
	dprintk(" +---------------------------------------------+\n");
	dprintk("  - Waiting 0x12341234 in MailboxAB ...");
	while(1) {
		if(*onedram_mailboxAB == 0x12341234)
			break;
		msleep(10);
		dprintk(".");
		count++;
		if(count > 200)
		{
			printk("dpram_phone_image_load fail.....\n");
			return -1;
		}
	}
	printk("DBL.\n");

    if (!dump_on)
    {
	    if(*onedram_sem == 0x1)
	 	    ReadPhoneFile(dbl_buf, dpram_base, MAX_DBL_IMG_SIZE, MAX_MODEM_IMG_SIZE, bml_part_no);
	    else
		    printk("[OneDRAM] %s failed.. sem: %d\n", __func__, *onedram_sem);
    }
	else
	{
	    dprintk("CP DUMP MODE !!! \n");
	}
	return 0;

}
	
static int dpram_nvdata_load(struct _param_nv *param)
{
#ifdef CONFIG_KERNEL_DEBUG_SEC
	if(dump_on)
	{   
		printk(" [dpram_nvdata_load] : When CP is in upload mode, don't copy NV data!");
		return -1;
	}
#endif //CONFIG_KERNEL_DEBUG_SEC

	int                   nRet;
	unsigned char * 	  load_addr =  DPRAM_VBASE + DPRAM_DEFAULT_NV_DATA_OFFSET;

	dprintk(" +---------------------------------------------+\n");
	dprintk(" |                 LOAD DGS Info               |\n");
	dprintk(" +---------------------------------------------+\n");

	printk("[DPRAM] Start getting DGS info.\n");

	if (FSR_BML_Open(0, FSR_BML_FLAG_NONE) != FSR_BML_SUCCESS)
	{
		printk("[DPRAM] BML_Open Error !!\n");
		while(1);
	}

	dprintk("[DPRAM] Try to read DGS Info.\n");

	/* Acquire SM */
	FSR_BML_AcquireSM(0);

	/* 
	 * Read DGS Page 
	 * Read 5th Page of 2047 Block
	 */
#ifdef CONFIG_USE_KMALLOC
	/*
	 * Modified for FSR Read DMA
	 * Use kmalloc buffer to read data by using FSR_BML_Read
	 */
	unsigned char *temp = kmalloc(4096, GFP_KERNEL);
	nRet = FSR_OND_4K_Read(0, 2047, 5, temp, NULL, FSR_LLD_FLAG_ECC_OFF);	
	memcpy(aDGSBuf, temp, sizeof(aDGSBuf));
	kfree(temp);
#endif
	
	/* Release SM*/
	FSR_BML_ReleaseSM(0);

	if(nRet != FSR_LLD_SUCCESS)
	{
		printk("[DPRAM] Reading DGS information failed !!\n");
		while(1);
	}

	dprintk("[DPRAM] DSG buffer =  %s \n", aDGSBuf);
	
	if(*onedram_sem != 0x1)
	{
		if(!onedram_get_semaphore(__func__)) 
		{
			printk("[OneDRAM] %s failed.. onedram_get_semaphore\n", __func__);
			return -1;
		}

	} 

	if(onedram_lock_with_semaphore(__func__) < 0)
	{
		printk("[OneDRAM] %s failed.. onedram_lock_with_semaphore\n", __func__);
		return -1;
	}

    //dprintk("[OneDRAM] 0x%x, 0x%x\n", DPRAM_VBASE, DPRAM_DGS_INFO_BLOCK_OFFSET);
	//WRITE_TO_DPRAM( DPRAM_DGS_INFO_BLOCK_OFFSET, aDGSBuf, DPRAM_DGS_INFO_BLOCK_SIZE);   //we don't need to copy DGS info to OneDRAM 

	printk("DGS\n");

	dprintk(" +---------------------------------------------+\n");
	dprintk(" |                  LOAD NVDATA                |\n");
	dprintk(" +---------------------------------------------+\n");
	dprintk("  - Read from File(\"/efs/nv_data.bin\").\n");
	dprintk("  - Address of NV data(virt): 0x%08x.\n", param->addr);
	dprintk("  - Size of NV data: %d.\n", param->size);

    WRITE_TO_DPRAM( 0xF80000 - 0x5000, param->addr, param->size);

	onedram_release_lock(__func__);

	return_onedram_semaphore(__func__);

	dprintk("NV \n");

	return 0;
}

#if 0 // kt.hur this action is replaced by Remote File System
static void dpram_nvpacket_data_read(void * usrbufaddr)
{
	unsigned int val = 0;
	unsigned char * packet_addr =  DPRAM_VBASE + DPRAM_NV_PACKET_DATA_ADDRESS;
	unsigned char * cur_ptr;
	signed int length;
	unsigned int err_length = 0xffffffff;
	
	if(*onedram_sem != 0x1)
		{
		if(!onedram_get_semaphore(__func__)) 
			{
			copy_to_user((void __user *)usrbufaddr, (unsigned char*)&err_length, sizeof(unsigned int));
			printk("[OneDRAM] %s failed.. onedram_get_semaphore\n", __func__);
			return;
			}
		}

	if(onedram_lock_with_semaphore(__func__) < 0)
		{
		copy_to_user((void __user *)usrbufaddr, (unsigned char*)&err_length, sizeof(unsigned int));
		printk("[OneDRAM] %s failed.. onedram_lock_with_semaphore\n", __func__);
		return;
		}
	
	if (*onedram_sem == 0x1)	// check twice
		{
		// DATA STRUCTURE
		// 0x7f  + length 4byte + data + 0x7e
		cur_ptr = packet_addr;
		if ( *cur_ptr++ != 0x7f )
			{
			copy_to_user((void __user *)usrbufaddr, (unsigned char*)&err_length, sizeof(unsigned int));
			printk("[OneDRAM] %s failed.. invalid header\n", __func__);
			}

		// big endian
		length = *cur_ptr++;
		length |= (*cur_ptr++)<<8;
		length |= (*cur_ptr++)<<16;
		length |= (*cur_ptr++)<<24;


		printk(KERN_DEBUG "[OneDRAM] %s nv packet large data length = 0x%x\n", __func__,length);

		copy_to_user((void __user *)usrbufaddr, (unsigned char*)&length, sizeof(length));
		copy_to_user((void __user *)(usrbufaddr+sizeof(length)), cur_ptr, length);

		cur_ptr += length;
		
		// check EOP 0x7e 
		if ( *cur_ptr != 0x7e )
			{
			copy_to_user((void __user *)usrbufaddr, (unsigned char*)&err_length, sizeof(unsigned int));
			printk("[OneDRAM] %s failed.. invalid header tail\n", __func__);
			}


		printk(KERN_DEBUG "[OneDRAM] %s nv packet large data read complete\n", __func__);
		}
	else
		{
		copy_to_user((void __user *)usrbufaddr, (unsigned char*)&err_length, sizeof(unsigned int));
		printk("[OneDRAM] %s failed.. sem: %d check and return\n", __func__, *onedram_sem);
		}

	onedram_release_lock(__func__);

	return_onedram_semaphore(__func__);

	return;
}
#endif

static void dpram_phone_power_on(void)
{
	boot_complete = 0;
	phone_start_ok = 0;
	kerneldebuglevel = kernel_sec_get_debug_level();
	printk(KERN_DEBUG "kernel debuglevel %x \n",kerneldebuglevel);
	if( phone_power_state ) {
		dprintk("[OneDram] phone off (before phone power on).\n");
		gpio_set_value(GPIO_PHONE_ON, GPIO_LEVEL_LOW);
	gpio_set_value(GPIO_CP_RST, GPIO_LEVEL_LOW);
		interruptible_sleep_on_timeout(&dpram_wait, 100);	//	msleep(500);
		dprintk("[OneDram] phone rst low 500ms).\n");
	}

	printk(KERN_DEBUG "[OneDram] phone power on.\n");
	printk(KERN_DEBUG "[OneDram] HWREV %d.\n",HWREV);

	gpio_set_value(GPIO_USIM_BOOT, GPIO_LEVEL_HIGH);
    mdelay(50);
    if (HWREV<=3)
        gpio_set_value(GPIO_FLM_SEL_R03, GPIO_LEVEL_HIGH);
    else
        gpio_set_value(GPIO_FLM_SEL_R04, GPIO_LEVEL_HIGH);

	if(phone_active_state)
	{
	gpio_set_value(GPIO_CP_RST, GPIO_LEVEL_LOW);
	}
	interruptible_sleep_on_timeout(&dpram_wait, 40);	//	msleep(200);

	gpio_set_value(GPIO_PHONE_ON, GPIO_LEVEL_HIGH);
	interruptible_sleep_on_timeout(&dpram_wait, 6);		//  msleep(30)

	gpio_set_value(GPIO_CP_RST, GPIO_LEVEL_HIGH);
	interruptible_sleep_on_timeout(&dpram_wait, 100);	//	msleep(500);

	gpio_set_value(GPIO_PHONE_ON, GPIO_LEVEL_LOW);
	interruptible_sleep_on_timeout(&dpram_wait, 20);	//	msleep(100);

    if (HWREV<=3)
    {
        dprintk("  - GPIO_PHONE_ON : %s, GPIO_CP_RST : %s\n", 
        gpio_get_value(GPIO_PHONE_ON)?"HIGH":"LOW", gpio_get_value(GPIO_CP_RST)?"HIGH":"LOW");
        dprintk("  - GPIO_FLM_SEL : %s, GPIO_USIM_BOOT : %s\n", 
        gpio_get_value(GPIO_FLM_SEL_R03)?"HIGH":"LOW", gpio_get_value(GPIO_USIM_BOOT)?"HIGH":"LOW");
        phone_power_state = 1;
    }
    else
    {
    	dprintk("  - GPIO_PHONE_ON : %s, GPIO_CP_RST : %s\n", 
		gpio_get_value(GPIO_PHONE_ON)?"HIGH":"LOW", gpio_get_value(GPIO_CP_RST)?"HIGH":"LOW");
    	dprintk("  - GPIO_FLM_SEL : %s, GPIO_USIM_BOOT : %s\n", 
        gpio_get_value(GPIO_FLM_SEL_R04)?"HIGH":"LOW", gpio_get_value(GPIO_USIM_BOOT)?"HIGH":"LOW");
    	phone_power_state = 1;
    }	
#ifdef CONFIG_KERNEL_DEBUG_SEC
	// If it is configured to dump both AP and CP, reset both AP and CP here.
	if(kernel_sec_dump_ap_cp)
	{
		kernel_sec_dump_cp_handle2();
	}
#endif	
}

	
static int dpram_phone_boot_start(void)
{
	unsigned int send_mail;

#if defined(CONFIG_KERNEL_DEBUG_SEC)
	if(!dump_on)
	    send_mail = KERNEL_SEC_DUMP_CP_MODEM_IMG_COPY_DONE;
	else
		send_mail = KERNEL_SEC_DUMP_CP_UPLOAD_MAGIC_CODE;

	*onedram_sem = 0x0;
	*onedram_mailboxBA = send_mail;

    dprintk("[dpram_phone_boot_start] : Phone Boot Start..\n");
    
    if(!dump_on)
    {
	    dprintk(" +---------------------------------------------+\n");
	    dprintk(" |   FINISH IMAGE LOAD  &  START PHONE BOOT    |\n");
	    dprintk(" +---------------------------------------------+\n");
	    dprintk("  - Send to MailboxBA 0x%8x\n", send_mail);
	    dprintk("  - Waiting 0xabcdabcd in MailboxAB ...");
           modem_wait_count = 0;
		while(1)
		{
			if(boot_complete == 1) 
			{
				printk("Done.\n");
				dprintk(" +---------------------------------------------+\n");
				dprintk(" |             PHONE BOOT COMPLETE             |\n");
				dprintk(" +---------------------------------------------+\n");
                                printk(" modem_wait_count : %d \n", modem_wait_count);
				break;
			}
			msleep(10);
			if(++modem_wait_count > 1000)
			{
				//panic("Modem Boot Fail!!!");
				printk("[dpram_phone_boot_start] Modem Boot Fail! Modem size Max check!! \n");
				return -1;				
			}
		}
		printk("Start\n");
	}
	else
	{
        int loop_count;
        
	    printk("\n[dpram_phone_boot_start] : CP Dump State, Waiting 1st Data");

        loop_count =1;
        
        while(1)
        {
            // Check for valid cmd to copy or end copy!
            if(!cp_upload_cmd_rcvd)
            {
                printk("<%d>",cp_upload_cmd_rcvd);
                msleep(100);
                continue;
            }
                
            printk("\nGOT DATA... %d",loop_count);

            // Store data to FileSystem
            dpram_cp_dump(loop_count);

            printk("------> Data stored \n");

            // Update MBX_BA and release semaphore
            cp_upload_cmd_rcvd=0;
            *onedram_sem = 0x0; 
			*onedram_mailboxBA = KERNEL_SEC_DUMP_CP_STORE_CMD_COPY_DONE;

            // Received complete CP Dump
            if(*onedram_mailboxAB == KERNEL_SEC_DUMP_CP_STORE_CMD_COPY_LAST)
                break;

            loop_count++;
	    }

        // CP dump completed.
        dump_on=0;
	}
#else
	if(!dump_on)
	    send_mail = 0x45674567;
	else
		send_mail = 0x34563456;

	*onedram_sem = 0x0;
	*onedram_mailboxBA = send_mail;

    if(!dump_on)
    {
	    printk(" +---------------------------------------------+\n");
	    printk(" |   FINISH IMAGE LOAD  &  START PHONE BOOT    |\n");
	    printk(" +---------------------------------------------+\n");
	    printk("  - Send to MailboxBA 0x%8x\n", send_mail);
	    printk("  - Waiting 0xabcdabcd in MailboxAB ...");
	    modem_wait_count = 0;
	    while(1)
		{
		    if(boot_complete == 1) 
		    {
			    printk("Done.\n");
			    printk(" +---------------------------------------------+\n");
			    printk(" |             PHONE BOOT COMPLETE             |\n");
			    printk(" +---------------------------------------------+\n");
                         printk(" modem_wait_count : %d \n", modem_wait_count);
			    //printk("  - MailboxAB: 0x%8x\n", *onedram_mailboxAB);
			    //boot_complete = 1;
			    break;
		    }
		    msleep(10);
		    printk(".");
			if(++modem_wait_count > 500)
				panic("Modem Boot Fail!!!");
	    }
		printk("\n");
    }
	else
	{
	    // DUMP MODE

		// FIRST DATA
	    printk("Waiting 1st Data");
	    while(gpio_get_value(GPIO_nINT_ONEDRAM_AP))
			printk(".");
		printk("\n");

		if(*onedram_mailboxAB == 0x23452345)
		{
		    printk("GOT DATA\n");

			if(*onedram_sem == 0x1)
			{
				dpram_cp_dump(FIRST);

	            *onedram_sem = 0x0;
				*onedram_mailboxBA = send_mail;
			}
			else
			{
				printk("NEED SMP\n");
				while(1);
			}
				
		}
		else
		{
		    printk("STRANGE\n");
			while(1);
		}

        // SECOND DATA 
		printk("Waiting 2nd Data");
	    while(gpio_get_value(GPIO_nINT_ONEDRAM_AP))
			printk(".");
		printk("\n");

		if(*onedram_mailboxAB == 0x23452345)
		{
		    printk("GOT DATA\n");

			if(*onedram_sem == 0x1)
			{
				dpram_cp_dump(SECOND);

	            *onedram_sem = 0x0;
				*onedram_mailboxBA = send_mail;
			}
			else
			{
				printk("NEED SMP\n");
				while(1);
			}
				
		}
		else
		{
		    printk("STRANGE\n");
			while(1);
		}

		// THIRD DATA 
		printk("Waiting 3rd Data");
	    while(gpio_get_value(GPIO_nINT_ONEDRAM_AP))
			printk(".");
		printk("\n");

		if(*onedram_mailboxAB == 0x23452345)
		{
		    printk("GOT DATA\n");

			if(*onedram_sem == 0x1)
			{
				dpram_cp_dump(THIRD);
			}
			else
			{
				printk("NEED SMP\n");
				while(1);
			}
				
		}
		else
		{
		    printk("STRANGE\n");
			while(1);
		}    
	}
#endif //CONFIG_KERNEL_DEBUG_SEC    
	return 0;
}

static void dpram_phone_power_off(void)
{
	printk(KERN_DEBUG "[OneDRAM] Phone power Off. - do nothing\n");
	disable_irq(IRQ_PHONE_ACTIVE);
	boot_complete = 0;
	phone_power_state = 0;
	phone_start_ok = 0;
	phone_shutdown = 1;
}

static int dpram_phone_getstatus(void)
{
	return gpio_get_value(GPIO_PHONE_ACTIVE);
}

static void dpram_phone_reset(void)
{
    printk(KERN_DEBUG "[OneDRAM] Phone power Reset.\n");
	if(*onedram_sem == 0x1) {
		dprintk("[OneDRAM} semaphore: %d\n", *onedram_sem);
		*onedram_sem = 0x00;
	}
	gpio_set_value(GPIO_CP_RST, GPIO_LEVEL_LOW);
	msleep(100);
	gpio_set_value(GPIO_CP_RST, GPIO_LEVEL_HIGH);

	// Wait until phone is stable
	msleep(200);
}

static void dpram_mem_rw(struct _mem_param *param)
{
}

static int dpram_phone_ramdump_on(void)
{
#ifdef CONFIG_KERNEL_DEBUG_SEC 
	const u16 rdump_flag1 = 0xDEAD;
	const u16 rdump_flag2 = 0xDEAD;
	const u16 temp1, temp2;
	
	dprintk("[OneDRAM] Ramdump ON.\n");

	if(!onedram_get_semaphore(__func__)) 
		return -1;

	if(onedram_lock_with_semaphore(__func__) < 0)
		return -1;

	WRITE_TO_DPRAM(DPRAM_MAGIC_CODE_ADDRESS,    &rdump_flag1, sizeof(rdump_flag1));
	WRITE_TO_DPRAM(DPRAM_ACCESS_ENABLE_ADDRESS, &rdump_flag2, sizeof(rdump_flag2));

	READ_FROM_DPRAM((void *)&temp1, DPRAM_MAGIC_CODE_ADDRESS, sizeof(temp1));
	READ_FROM_DPRAM((void *)&temp2, DPRAM_ACCESS_ENABLE_ADDRESS, sizeof(temp2));
	printk(KERN_DEBUG "[OneDRAM] flag1: %x flag2: %x\n", temp1, temp2);

	/* @LDK@ send init end code to phone */
	onedram_release_lock(__func__);


	dump_on = 1;

	return_onedram_semaphore(__func__);
	if(*onedram_sem == 0x1) {
		printk(KERN_DEBUG "[OneDRAM] Failed to return semaphore. try again\n");
		*onedram_sem = 0x00;
	}

	return 0;
#endif
}

static int dpram_phone_ramdump_off(void)
{
#ifdef CONFIG_KERNEL_DEBUG_SEC 

	dump_on = 0;
	phone_sync = 0;
	

	return 0;
#endif
}
#ifdef CONFIG_KERNEL_DEBUG_SEC
static struct file* cp_dump_open_file(char *filename, int mode)
{
    struct file *file_p=NULL;
    ssize_t wc;
    int old_fs = get_fs();

    set_fs(KERNEL_DS);

    file_p = filp_open((const char*)filename, O_WRONLY | O_CREAT | O_APPEND, 00666);

    if (IS_ERR(file_p))
    {
        file_p = NULL;
        printk("\n %s open failed(%d)",ERR_PTR(file_p));
        set_fs(old_fs);    
        return file_p;
    }

    set_fs(old_fs); 

    return file_p;
}

static void cp_dump_close_file(struct file *file_p)
{
    if(file_p)
        filp_close(file_p,NULL);
}

char test_filewrite_buf[] = "This is CP dump";

static void cp_dump_write_file(struct file *file_p,void **pdata, int len)
{
    if(file_p)
    {
        int old_fs;
        
        old_fs=get_fs();
        set_fs(KERNEL_DS);
        vfs_write(file_p,(char __user*)*pdata,len,&file_p->f_pos);
        set_fs(old_fs);
    }
    
    return;
}
#endif //CONFIG_KERNEL_DEBUG_SEC

/* CP Dump Logic */
static void dpram_cp_dump(dump_order order)
{
#ifdef CONFIG_KERNEL_DEBUG_SEC

    int mode;
    static char cp_dump_filename[KERNEL_SEC_DUMP_CP_FILE_NAME_MAX];
    int size_to_copy=0;
    void *pCpData=NULL;
    int buf_size = KERNEL_SEC_DUMP_CP_BUF_SIZE_READ_UNIT;
    int dpram_offset=0x0;
    int bytes_remained=0;
    static struct file *file_p;
    
    printk(KERN_DEBUG "\n[dpram_cp_dump] : order =%d\n",order);

    if(order == FIRST)
    {
        size_to_copy = KERNEL_SEC_DUMP_CP_SIZE1_BYTES;
        mode = O_WRONLY|O_CREAT;
        sprintf(cp_dump_filename,KERNEL_SEC_DUMP_CP_FILE_NAME,cpu_clock(0));    
        file_p = cp_dump_open_file(cp_dump_filename,mode);    
    }
    else if (order == SECOND)
    {
        size_to_copy = KERNEL_SEC_DUMP_CP_SIZE2_BYTES;
        mode = O_APPEND;        
    }
    else if (order == THIRD)
    {
        size_to_copy = KERNEL_SEC_DUMP_CP_SIZE3_BYTES;
        mode = O_APPEND;        
    }

    bytes_remained = size_to_copy;

    pCpData = kmalloc(buf_size,GFP_USER);    
    
    if(pCpData == NULL)
    {
        printk(KERN_DEBUG "\n Unable to allocate %d bytes memory!",buf_size);
        return;
    }
   
    while(bytes_remained > 0)
    {
        if(bytes_remained < buf_size)
        {
            READ_FROM_DPRAM(pCpData,dpram_offset,bytes_remained);
            cp_dump_write_file(file_p,(void*)&pCpData,bytes_remained);
            printk(KERN_DEBUG "-->finish");
            break;
        }
        else
        {
            READ_FROM_DPRAM(pCpData,dpram_offset,buf_size);
            cp_dump_write_file(file_p,(void*)&pCpData,buf_size);
        }

        bytes_remained = bytes_remained - buf_size;
        dpram_offset = dpram_offset + buf_size;
        printk(KERN_DEBUG ".");
    }

    if(order == THIRD)
    {
        cp_dump_close_file(file_p);
    }

    if(pCpData)
        kfree(pCpData);

    return;
#else
    int                   nErr = BML_SUCCESS;
    unsigned int          nVol = 0;
    
    unsigned int          nCPLogStartBlock;
	unsigned int          nNumCPLogBlocks;

    unsigned int          pPartBuf[2] = {0, 0};
	unsigned int          pBufIn[3] = {0, 0, 0};
	unsigned int          i;
	unsigned int          nBytesReturned = 0;

	unsigned int          nSctsPerPg;
    unsigned int          nSctsPerBlk;

	XSRPartEntry          stPartEntry;
	BMLVolSpec            stVolSpec;

	unsigned int          *pSrcAddr = (unsigned int *)DPRAM_VBASE;

	unsigned int          nNumOfLogSector0 = (((16 * 1024 * 1024) - (2 * 1024)) / XSR_SECTOR_SIZE);
	unsigned int          nNumOfLogSector1 = ((2 * 1024 * 1024) / XSR_SECTOR_SIZE);

	unsigned int          nBlkNum, nSctNum;
    unsigned int          nStartVbn, nEndVbn;
    unsigned int          nStartVsn, nEndVsn;
	

	/* BML_Open */
	if (BML_Open(nVol) != BML_SUCCESS)
	{
	    dprintk("BML_Open fail!!!\n");
	    while(1);
	}
			
	/* BML_LoadPIEntry */
	nErr = BML_LoadPIEntry(nVol, TEMP_PART_ID, &stPartEntry);
	if(nErr != BML_SUCCESS)
	{
	    dprintk("BML_LoadPIEntry fail!!!\n");
	    while(1);
	}
	else
	{
	    nCPLogStartBlock = stPartEntry.n1stVbn;
	    nNumCPLogBlocks = stPartEntry.nNumOfBlks;
	}


    if(order == FIRST)
    {
        dprintk("BML_IOCtl - BML_IOCTL_CHANGE_PART_ATTR\n");
        pPartBuf[0] = TEMP_PART_ID;
    	pPartBuf[1] = BML_PI_ATTR_RW;
    	
        nErr = BML_IOCtl(0, BML_IOCTL_CHANGE_PART_ATTR,
    		(unsigned char *)&pPartBuf, sizeof(unsigned int) * 2, NULL, 0, &nBytesReturned);
    	if (nErr != BML_SUCCESS) 
    	{
    		dprintk("BML_EraseBlk fail!!!\n");
    		while(1);
    	}
    	
        /* BML_EraseBlk */
    	dprintk("BML_EraseBlk\n");
    	for(i=0 ; i < nNumCPLogBlocks ; i++)
    	{
    		nErr = BML_EraseBlk(0, (i+ nCPLogStartBlock), BML_FLAG_SYNC_OP);
    		if(nErr != BML_SUCCESS)
    		{
    		    dprintk("BML_EraseBlk fail!!!\n");
    		    while(1);
    		}
    	}
    }
	
    /* BML_GetVolInfo */
	dprintk("BML_GetVolInfo\n");
	if (BML_GetVolInfo(nVol, &stVolSpec) != BML_SUCCESS)
    {
        dprintk("BML_GetVolInfo fail!!!\n");
        while(1);
    }
	else
	{
	    nSctsPerPg  = stVolSpec.nSctsPerPg;
        nSctsPerBlk = stVolSpec.nPgsPerBlk * stVolSpec.nSctsPerPg;
	}

	   
    /* Write the first CP Log */
	dprintk("BML_Write\n");
    if (order == FIRST)
        nStartVsn = (nCPLogStartBlock) * nSctsPerBlk;
	else if (order == SECOND)
		nStartVsn = (nCPLogStartBlock) * nSctsPerBlk + nNumOfLogSector0;
	else
		nStartVsn = (nCPLogStartBlock) * nSctsPerBlk + (2 * nNumOfLogSector0);
	
	if (order == THIRD)
        nEndVsn   = nStartVsn + nNumOfLogSector1;
	else
        nEndVsn   = nStartVsn + nNumOfLogSector0;

	dprintk("nStartVsn = %d\n", nStartVsn);
	dprintk("nEndVsn = %d\n", nEndVsn);
	
    for (nSctNum = nStartVsn; nSctNum < nEndVsn; nSctNum += nSctsPerPg)
    {
        nErr = BML_Write(nVol,                                  /* Volum Number             */
                         nSctNum,                               /* Sector Number            */
                         nSctsPerPg,                            /* Number of Sectors to Write */
                         pSrcAddr,                              /* Pointer to Main data     */
                         NULL,                                  /* Pointer to Spare data    */
                         BML_FLAG_SYNC_OP | BML_FLAG_ECC_ON);   /* Operation Flag           */

        if (nErr != BML_SUCCESS)
        {
            dprintk("BML_Write fail!!!\n");
            while(1);
        }

        pSrcAddr += (XSR_SECTOR_SIZE * nSctsPerPg) / 4;
    }

	/* BML_Close */
	if (BML_Close(nVol) != BML_SUCCESS)
	{
	    dprintk("BML_Close fail!!!\n");
	    while(1);
	}

	dprintk("BML Operation Done!!!\n");
#endif
}

typedef struct mem_ipc_4_1 {
	u8    info[64];
	u8    fmt_rx[4096];
	u8    fmt_tx[4096];
} t_mem_ipc_4_1;

static int dpram_phone_mdump(void* userbuf) {
	if (*onedram_sem) {
		atomic_inc(&onedram_lock);
		t_mem_ipc_4_1* mem_ipc_4_1_user = (t_mem_ipc_4_1*)userbuf;
		copy_to_user((void __user *)mem_ipc_4_1_user->info, DPRAM_VBASE, 64);
		copy_to_user((void __user *)mem_ipc_4_1_user->fmt_tx, DPRAM_VBASE + DPRAM_PDA2PHONE_FORMATTED_BUFFER_ADDRESS , DPRAM_FORMATTED_BUFFER_SIZE);
		copy_to_user((void __user *)mem_ipc_4_1_user->fmt_rx, DPRAM_VBASE + DPRAM_PHONE2PDA_FORMATTED_BUFFER_ADDRESS, DPRAM_FORMATTED_BUFFER_SIZE);
		onedram_release_lock(__func__);
		return 0;
	} else {
		/* request semaphore */
		const u16 cmd = INT_COMMAND(INT_MASK_CMD_SMP_REQ);
		*onedram_mailboxBA = cmd;

		return -EAGAIN;
	}
}


#ifdef CONFIG_PROC_FS
static int dpram_read_proc(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	char *p = page;
	int len;
	u32 sem;
	u32 fmt_in_head = 0, fmt_in_tail = 0, fmt_out_head = 0, fmt_out_tail = 0;
	u32 raw_in_head = 0, raw_in_tail = 0, raw_out_head = 0, raw_out_tail = 0;
	u32 in_interrupt = 0, out_interrupt = 0;

#ifdef _ENABLE_ERROR_DEVICE
	char buf[DPRAM_ERR_MSG_LEN];
	unsigned long flags;
#endif	/* _ENABLE_ERROR_DEVICE */
	
	if(!onedram_get_semaphore(__func__)) 
		return -1;
	sem = *onedram_sem;
	if (sem == 1) {
		READ_FROM_DPRAM((void *)&fmt_in_head, DPRAM_PHONE2PDA_FORMATTED_HEAD_ADDRESS, 
				sizeof(fmt_in_head));
		READ_FROM_DPRAM((void *)&fmt_in_tail, DPRAM_PHONE2PDA_FORMATTED_TAIL_ADDRESS, 
				sizeof(fmt_in_tail));
		READ_FROM_DPRAM((void *)&fmt_out_head, DPRAM_PDA2PHONE_FORMATTED_HEAD_ADDRESS, 
				sizeof(fmt_out_head));
		READ_FROM_DPRAM((void *)&fmt_out_tail, DPRAM_PDA2PHONE_FORMATTED_TAIL_ADDRESS, 
				sizeof(fmt_out_tail));

		READ_FROM_DPRAM((void *)&raw_in_head, DPRAM_PHONE2PDA_RAW_HEAD_ADDRESS, 
				sizeof(raw_in_head));
		READ_FROM_DPRAM((void *)&raw_in_tail, DPRAM_PHONE2PDA_RAW_TAIL_ADDRESS, 
				sizeof(raw_in_tail));
		READ_FROM_DPRAM((void *)&raw_out_head, DPRAM_PDA2PHONE_RAW_HEAD_ADDRESS, 
				sizeof(raw_out_head));
		READ_FROM_DPRAM((void *)&raw_out_tail, DPRAM_PDA2PHONE_RAW_TAIL_ADDRESS, 
				sizeof(raw_out_tail));
	}
	else
	{
		printk(KERN_DEBUG "semaphore:%x \n", sem);
	}

	in_interrupt = *onedram_mailboxAB;
	out_interrupt = *onedram_mailboxBA;

#ifdef _ENABLE_ERROR_DEVICE
	memset((void *)buf, '\0', DPRAM_ERR_MSG_LEN);
	local_irq_save(flags);
	memcpy(buf, dpram_err_buf, DPRAM_ERR_MSG_LEN - 1);
	local_irq_restore(flags);
#endif	/* _ENABLE_ERROR_DEVICE */

	p += sprintf(p,
			"-------------------------------------\n"
			"| NAME\t\t\t| VALUE\n"
			"-------------------------------------\n"
			"| Onedram Semaphore\t| %d\n"
			"| requested Semaphore\t| %d\n"

			"-------------------------------------\n"
			"| PDA->PHONE FMT HEAD\t| %d\n"
			"| PDA->PHONE FMT TAIL\t| %d\n"
			"-------------------------------------\n"
			"| PDA->PHONE RAW HEAD\t| %d\n"
			"| PDA->PHONE RAW TAIL\t| %d\n"
			"-------------------------------------\n"
			"| PHONE->PDA FMT HEAD\t| %d\n"
			"| PHONE->PDA FMT TAIL\t| %d\n"
			"-------------------------------------\n"
			"| PHONE->PDA RAW HEAD\t| %d\n"
			"| PHONE->PDA RAW TAIL\t| %d\n"

			"-------------------------------------\n"
			"| PHONE->PDA MAILBOX\t| 0x%04x\n"
			"| PDA->PHONE MAILBOX\t| 0x%04x\n"
			"-------------------------------------\n"
#ifdef _ENABLE_ERROR_DEVICE
			"| LAST PHONE ERR MSG\t| %s\n"
#endif	/* _ENABLE_ERROR_DEVICE */
			"-------------------------------------\n"
			"| PHONE ACTIVE\t\t| %s\n"
			"| DPRAM INT Level\t| %d\n"
			"| CP Reset Count\t\t| %d\n"
			"-------------------------------------\n",
			sem, requested_semaphore,

			fmt_out_head, fmt_out_tail,
			raw_out_head, raw_out_tail,
			fmt_in_head, fmt_in_tail,
			raw_in_head, raw_in_tail,
		
			in_interrupt, out_interrupt,
#ifdef _ENABLE_ERROR_DEVICE
			(buf[0] != '\0' ? buf : "NONE"),
#endif	/* _ENABLE_ERROR_DEVICE */

			(dpram_phone_getstatus() ? "ACTIVE" : "INACTIVE"),
				gpio_get_value(GPIO_nINT_ONEDRAM_AP),
			cp_silentreset_count
		);

	len = (p - page) - off;
	if (len < 0) {
		len = 0;
	}

	*eof = (len <= count) ? 1 : 0;
	*start = page + off;

	return len;
}
#endif /* CONFIG_PROC_FS */

/* dpram tty file operations. */
static int dpram_tty_open(struct tty_struct *tty, struct file *file)
{
	dpram_device_t *device = &dpram_table[tty->index];

	device->serial.tty = tty;
	device->serial.open_count++;

	if (device->serial.open_count > 1) {
		device->serial.open_count--;
		return -EBUSY;
	}

	tty->driver_data = (void *)device;
	tty->low_latency = 1;
	return 0;
}

static void dpram_tty_close(struct tty_struct *tty, struct file *file)
{
	dpram_device_t *device = (dpram_device_t *)tty->driver_data;

	if (device && (device == &dpram_table[tty->index])) {
		down(&device->serial.sem);
		device->serial.open_count--;
		device->serial.tty = NULL;
		up(&device->serial.sem);
	}
}

static int dpram_tty_write(struct tty_struct *tty,
		const unsigned char *buffer, int count)
{
	dpram_device_t *device = (dpram_device_t *)tty->driver_data;

	if (!device || dump_on) {
		return 0;
	}

	return dpram_write(device, buffer, count);
}

static int dpram_tty_write_room(struct tty_struct *tty)
{
	int avail;
	u32 head, tail;

	dpram_device_t *device = (dpram_device_t *)tty->driver_data;

	if (device != NULL) {
		head = device->out_head_saved;
		tail = device->out_tail_saved;
		avail = (head < tail) ? tail - head - 1 :
			device->out_buff_size + tail - head - 1;

		return avail;
	}

	return 0;
}


static int dpram_tty_ioctl(struct tty_struct *tty, struct file *file,
		unsigned int cmd, unsigned long arg)
{
	unsigned int val;

//	dprintk("START cmd = 0x%x\n", cmd);
	
	switch (cmd) {
		case DPRAM_PHONE_ON:
			dprintk(" +---------------------------------------------+\n");
			dprintk(" |   INIT ONEDRAM  &  READY TO TRANSFER DATA   |\n");
			dprintk(" +---------------------------------------------+\n");
			dprintk("  - received DPRAM_PHONE_ON ioctl.\n");

			phone_sync = 0;
			dump_on = 0;

			if(boot_complete) {
				if(dpram_init_and_report() < 0) {
					printk("  - Failed.. unexpected error when ipc transfer start.\n");
					return -1;
				}
				dprintk("  - OK! IPC TRANSER START..!\n");
			}else {
				printk("  - Failed.. plz.. booting modem first.\n");
			}
			return 0;

		case DPRAM_PHONE_GETSTATUS:
			val = dpram_phone_getstatus();
			return copy_to_user((unsigned int *)arg, &val, sizeof(val));
			
		case DPRAM_PHONE_POWON:
			dpram_phone_power_on();
			return 0;

		case DPRAM_PHONEIMG_LOAD:
		{
			int bml_part_no = -1;

			if(arg)
			{
				val = copy_from_user((void *)&bml_part_no, (void *)arg, sizeof(bml_part_no));
			}

			dprintk("  - Phone Img BML Part No %d\n",bml_part_no);
			return dpram_phone_image_load(bml_part_no);
		}

		case DPRAM_PHONE_BOOTSTART:
			return dpram_phone_boot_start();

		case DPRAM_NVDATA_LOAD:
		{
			struct _param_nv param;

			val = copy_from_user((void *)&param, (void *)arg, sizeof(param));
			return dpram_nvdata_load(&param);
		}
#if 0 // kt.hur this action is replaced by Remote File System
		case DPRAM_NVPACKET_DATAREAD:
			dpram_nvpacket_data_read((void *)arg);
			return 0;
#endif
#if defined(CONFIG_ONEDRAM_DGSINFO)
        case DPRAM_GET_DGS_INFO:
        {
            // Copy data to user
            printk(KERN_DEBUG "[DPRAM] Sending DGS info.\n");
            val = copy_to_user((void __user *)arg, aDGSBuf, DPRAM_DGS_INFO_BLOCK_SIZE);
			return 0;
        }
#endif
		case DPRAM_PHONE_RESET:
			dpram_phone_reset();
			return 0;

		case DPRAM_PHONE_OFF:
			dpram_phone_power_off();
			return 0;

		case DPRAM_PHONE_RAMDUMP_ON:
			dpram_phone_ramdump_on();
			return 0;

		case DPRAM_PHONE_RAMDUMP_OFF:
			dpram_phone_ramdump_off();
			return 0;

		case DPRAM_PHONE_MDUMP:
			return dpram_phone_mdump((void *)arg);

		case DPRAM_PHONE_UNSET_UPLOAD:
			kernel_sec_clear_upload_magic_number();
			return 0;

		case DPRAM_PHONE_SET_AUTOTEST:
			kernel_sec_set_autotest();
			return 0;

		case DPRAM_PHONE_SET_DEBUGLEVEL:
			switch(kernel_sec_get_debug_level_from_param())
			{
				case KERNEL_SEC_DEBUG_LEVEL_LOW:
					kernel_sec_set_debug_level(KERNEL_SEC_DEBUG_LEVEL_MID);
					kerneldebuglevel = KERNEL_SEC_DEBUG_LEVEL_MID;
					break;
				case KERNEL_SEC_DEBUG_LEVEL_MID:
					kernel_sec_set_debug_level(KERNEL_SEC_DEBUG_LEVEL_HIGH);
					kerneldebuglevel = KERNEL_SEC_DEBUG_LEVEL_HIGH;
					break;
				case KERNEL_SEC_DEBUG_LEVEL_HIGH:
					kernel_sec_set_debug_level(KERNEL_SEC_DEBUG_LEVEL_LOW);
					kerneldebuglevel = KERNEL_SEC_DEBUG_LEVEL_LOW;
					break;
				default:
					break;
			}
			return 0;
			
		case DPRAM_PHONE_GET_DEBUGLEVEL:
		{
			switch(kernel_sec_get_debug_level_from_param())
			{
				case KERNEL_SEC_DEBUG_LEVEL_LOW:
					val = 0xA0A0; 
					break;
				case KERNEL_SEC_DEBUG_LEVEL_MID:
					val = 0xB0B0;
					break;
				case KERNEL_SEC_DEBUG_LEVEL_HIGH:
					val = 0xC0C0;
					break;
				default:
					val = 0xFFFF;
					break;
			}
			//printk(KERN_DEBUG "DPRAM_PHONE_GET_DEBUGLEVEL : %x, %d\n", kernel_sec_get_debug_level_from_param(), val);
			return copy_to_user((unsigned int *)arg, &val, sizeof(val));
		}

		case DPRAM_PHONE_MMS_NOTI:
			receive_mms = 1;
			printk("Receive MMS \n");
			return 0;

		default:
			dprintk("Unknown Cmd: %x\n", cmd);
			break;
	}

	return -ENOIOCTLCMD;
}

static int dpram_tty_chars_in_buffer(struct tty_struct *tty)
{
	int data;
	u32 head, tail;

	dpram_device_t *device = (dpram_device_t *)tty->driver_data;

	if (device != NULL) {
		head = device->out_head_saved;
		tail = device->out_tail_saved;
		data = (head > tail) ? head - tail - 1 :
			device->out_buff_size - tail + head;

		return data;
	}

	return 0;
}

#ifdef _ENABLE_ERROR_DEVICE
static int dpram_err_read(struct file *filp, char *buf, size_t count, loff_t *ppos)
{
	DECLARE_WAITQUEUE(wait, current);

	unsigned long flags;
	ssize_t ret;
	size_t ncopy;

	add_wait_queue(&dpram_err_wait_q, &wait);
	set_current_state(TASK_INTERRUPTIBLE);

	while (1) {
		local_irq_save(flags);

		if (dpram_err_len) {
			ncopy = min(count, dpram_err_len);

			if (copy_to_user(buf, dpram_err_buf, ncopy)) {
				ret = -EFAULT;
			}

			else {
				ret = ncopy;
			}

			dpram_err_len = 0;
			
			local_irq_restore(flags);
			break;
		}

		local_irq_restore(flags);

		if (filp->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			break;
		}

		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}

		schedule();
	}

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&dpram_err_wait_q, &wait);

	return ret;
}

static int dpram_err_fasync(int fd, struct file *filp, int mode)
{
	return fasync_helper(fd, filp, mode, &dpram_err_async_q);
}

static unsigned int dpram_err_poll(struct file *filp,
		struct poll_table_struct *wait)
{
	poll_wait(filp, &dpram_err_wait_q, wait);
	return ((dpram_err_len) ? (POLLIN | POLLRDNORM) : 0);
}
#endif	/* _ENABLE_ERROR_DEVICE */

/* handlers. */
static void res_ack_tasklet_handler(unsigned long data)
{
	dpram_device_t *device = (dpram_device_t *)data;

	if (device && device->serial.tty) {
		struct tty_struct *tty = device->serial.tty;

		if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
				tty->ldisc->ops->write_wakeup) {
			(tty->ldisc->ops->write_wakeup)(tty);
		}

		wake_up_interruptible(&tty->write_wait);
	}
}

static void send_tasklet_handler(unsigned long data)
{
	dpram_tasklet_data_t *tasklet_data = (dpram_tasklet_data_t *)data;

	dpram_device_t *device = tasklet_data->device;
	u16 non_cmd = tasklet_data->non_cmd;

	int ret = 0;

	if (device != NULL && device->serial.tty) {
		struct tty_struct *tty = device->serial.tty;
			if(non_cmd & INT_MASK_SEND_RFS)
				ret = dpram_read_rfs(device, non_cmd);
			else
			ret = dpram_read(device, non_cmd);
			if (ret == -EAGAIN && phone_shutdown == 0) {
				if (non_cmd & INT_MASK_SEND_F) tasklet_schedule(&fmt_send_tasklet);
				if (non_cmd & INT_MASK_SEND_R) tasklet_schedule(&raw_send_tasklet);
				if (non_cmd & INT_MASK_SEND_RFS) tasklet_schedule(&rfs_send_tasklet);
				return ;
			}
      if( tty->index != 1)	//index : 0=dpram0, 1=dpram1, 2=rfs	
		tty_flip_buffer_push(tty);
	}

	else {
		dpram_drop_data(device);
	}
}

static void cmd_req_active_handler(void)
{
	send_interrupt_to_phone_with_semaphore(INT_COMMAND(INT_MASK_CMD_RES_ACTIVE));
}

static void cmd_error_display_handler(void)
{

#ifdef _ENABLE_ERROR_DEVICE
	char buf[DPRAM_ERR_MSG_LEN];
	unsigned long flags;

	phone_sync = 0;

		u32 mailbox_AB=0x00;

	memset((void *)buf, 0, sizeof (buf));

	if (!dpram_phone_getstatus()) {
		memcpy((void *)buf, "8 $PHONE-OFF", sizeof("8 $PHONE-OFF"));
	} else {
		buf[0] = '1';
		buf[1] = ' ';

		if(*onedram_sem == 0x1) {
			READ_FROM_DPRAM((buf + 2), DPRAM_PHONE2PDA_FORMATTED_BUFFER_ADDRESS, sizeof (buf) - 3);
		if (kernel_sec_dump_ap_cp != 1)
		{
			mailbox_AB=*onedram_mailboxAB;
				printk(KERN_DEBUG " - [cmd_error_display_handler] MAILBOX_CHECK MB_AB =0x%8x\n",mailbox_AB);
			if(mailbox_AB == KERNEL_SEC_DUMP_CP_DEAD_INDICATOR) 
			{
					printk(KERN_DEBUG " [cmd_error_display_handler] call kernel_sec_dump_cp_handle1\n",mailbox_AB);
				kernel_sec_dump_cp_handle1();			
			}
		}	
	} else {
			memcpy(buf+2, "Modem Error Fatal", sizeof("Modem Error Fatal"));
	}
	}
		printk(KERN_DEBUG "[PHONE ERROR] ->> %s\n", buf);

	local_irq_save(flags);
	memcpy(dpram_err_buf, buf, DPRAM_ERR_MSG_LEN);
	dpram_err_len = 64;
	local_irq_restore(flags);

	wake_up_interruptible(&dpram_err_wait_q);
	kill_fasync(&dpram_err_async_q, SIGIO, POLL_IN);

#endif	/* _ENABLE_ERROR_DEVICE */

}

static void cmd_phone_start_handler(void)
{
	printk(KERN_DEBUG "Phone Boot OK \n");
}

static void cmd_req_time_sync_handler(void)
{
	/* TODO: add your codes here.. */
}

static void cmd_phone_deep_sleep_handler(void)
{
	/* TODO: add your codes here.. */
}

static void cmd_nv_rebuilding_handler(void)
{
	/* TODO: add your codes here.. */
}

static void cmd_emer_down_handler(void)
{
	/* TODO: add your codes here.. */
}

static void cmd_smp_req_handler(void)
{
	const u16 cmd = INT_COMMAND(INT_MASK_CMD_SMP_REP);
	if(return_onedram_semaphore(__func__))
		*onedram_mailboxBA = cmd;
}

static void cmd_smp_rep_handler(void)
{
	u16 non_cmd;

	/* phone acked semaphore release */
	if(*onedram_sem != 0x0) {
		non_cmd = check_pending_rx();
		if (non_cmd)
		non_command_handler(non_cmd);
	}
}

static void semaphore_control_handler(unsigned long data)
{
	const u16 cmd = INT_COMMAND(INT_MASK_CMD_SMP_REP);

	if(return_onedram_semaphore(__func__))
		*onedram_mailboxBA = cmd;
}


static void command_handler(u16 cmd)
{
	switch (cmd) {
		case INT_MASK_CMD_REQ_ACTIVE:
			cmd_req_active_handler();
			break;

		case INT_MASK_CMD_ERR_DISPLAY:
			cmd_error_display_handler();
			break;

		case (INT_MASK_CMD_PHONE_START|INT_MASK_CP_QUALCOMM):
			cmd_phone_start_handler();
			phone_start_ok = 1;
			break;

		case INT_MASK_CMD_REQ_TIME_SYNC:
			cmd_req_time_sync_handler();
			break;

		case INT_MASK_CMD_PHONE_DEEP_SLEEP:
			cmd_phone_deep_sleep_handler();
			break;

		case INT_MASK_CMD_NV_REBUILDING:
			cmd_nv_rebuilding_handler();
			break;

		case INT_MASK_CMD_EMER_DOWN:
			cmd_emer_down_handler();
			break;

		case INT_MASK_CMD_SMP_REQ:
			tasklet_schedule(&semaphore_control_tasklet);
			break;

		case INT_MASK_CMD_SMP_REP:
			cmd_smp_rep_handler();
			break;

		default:
			dprintk("Unknown command.. %x\n", cmd);
	}
}

u16 check_pending_rx(void)
{
	u32 head, tail;
	u16 mbox = 0;

	if (*onedram_sem == 0)
		return 0;

	READ_FROM_DPRAM(&head, DPRAM_PHONE2PDA_FORMATTED_HEAD_ADDRESS, sizeof(head));
	READ_FROM_DPRAM(&tail, DPRAM_PHONE2PDA_FORMATTED_TAIL_ADDRESS, sizeof(tail));

	if (head != tail)
		mbox |= INT_MASK_SEND_F;

	/* @LDK@ raw check. */
	READ_FROM_DPRAM(&head, DPRAM_PHONE2PDA_RAW_HEAD_ADDRESS, sizeof(head));
	READ_FROM_DPRAM(&tail, DPRAM_PHONE2PDA_RAW_TAIL_ADDRESS, sizeof(tail));

	if (head != tail)
		mbox |= INT_MASK_SEND_R;

	/* @LDK@ raw check. */
	READ_FROM_DPRAM(&head, DPRAM_PHONE2PDA_RFS_HEAD_ADDRESS, sizeof(head));
	READ_FROM_DPRAM(&tail, DPRAM_PHONE2PDA_RFS_TAIL_ADDRESS, sizeof(tail));

	if (head != tail)
		mbox |= INT_MASK_SEND_RFS;

	return mbox;
	}

static void non_command_handler(u16 non_cmd)
{
	/* @LDK@ +++ scheduling.. +++ */
	if (non_cmd & INT_MASK_SEND_F) {
		wake_lock_timeout(&dpram_wake_lock, HZ/2);
		dpram_tasklet_data[FORMATTED_INDEX].device = &dpram_table[FORMATTED_INDEX];
		dpram_tasklet_data[FORMATTED_INDEX].non_cmd = (non_cmd & (INT_MASK_SEND_F |INT_MASK_REQ_ACK_F));
		
		fmt_send_tasklet.data = (unsigned long)&dpram_tasklet_data[FORMATTED_INDEX];
		tasklet_schedule(&fmt_send_tasklet);
	}

	if (non_cmd & INT_MASK_SEND_R) {
		wake_lock_timeout(&dpram_wake_lock, HZ*4);
		dpram_tasklet_data[RAW_INDEX].device = &dpram_table[RAW_INDEX];
		dpram_tasklet_data[RAW_INDEX].non_cmd = (non_cmd & (INT_MASK_SEND_R | INT_MASK_REQ_ACK_R));

		raw_send_tasklet.data = (unsigned long)&dpram_tasklet_data[RAW_INDEX];
		/* @LDK@ raw buffer op. -> soft irq level. */
		tasklet_schedule(&raw_send_tasklet);
	}

	if (non_cmd & INT_MASK_SEND_RFS) {
		wake_lock_timeout(&dpram_wake_lock, HZ*4);
		dpram_tasklet_data[RFS_INDEX].device = &dpram_table[RFS_INDEX];
		dpram_tasklet_data[RFS_INDEX].non_cmd = (non_cmd & (INT_MASK_SEND_RFS | INT_MASK_REQ_ACK_RFS));

		rfs_send_tasklet.data = (unsigned long)&dpram_tasklet_data[RFS_INDEX];
		/* @LDK@ raw buffer op. -> soft irq level. */
		tasklet_schedule(&rfs_send_tasklet);
	}

	if (non_cmd & INT_MASK_RES_ACK_F) {
		wake_lock_timeout(&dpram_wake_lock, HZ/2);
		tasklet_schedule(&fmt_res_ack_tasklet);
	}

	if (non_cmd & INT_MASK_RES_ACK_R) {
		wake_lock_timeout(&dpram_wake_lock, HZ*4);
		tasklet_schedule(&raw_res_ack_tasklet);
	}

	if (non_cmd & INT_MASK_RES_ACK_RFS) {
		wake_lock_timeout(&dpram_wake_lock, HZ*4);
		tasklet_schedule(&rfs_res_ack_tasklet);
	}

}

static inline
void check_int_pin_level(void)
{
	u16 mask = 0, cnt = 0;

	while (cnt++ < 3) {
		mask = *onedram_mailboxAB;
		if (gpio_get_value(GPIO_nINT_ONEDRAM_AP))
			break;
	}
}

/* @LDK@ interrupt handlers. */
static irqreturn_t dpram_irq_handler(int irq, void *dev_id)
{
#if defined(CONFIG_ONEDRAM_IRQPENDING)
	u32 mask;
#endif
	u16 irq_mask = 0;
	u32 mailboxAB;

    mailboxAB = *onedram_mailboxAB;
	irq_mask = (u16)mailboxAB;
	//check_int_pin_level();

#ifdef CONFIG_KERNEL_DEBUG_SEC

    if((mailboxAB == KERNEL_SEC_DUMP_CP_STORE_CMD_MORE) 
        || (mailboxAB == KERNEL_SEC_DUMP_CP_STORE_CMD_COPY_LAST))
    {
       if(dump_on)
       {
            cp_upload_cmd_rcvd=1;
            printk("\n CP Upload cmd received!.\n");
            printk("<<%d>>",cp_upload_cmd_rcvd);

       }
    }

	// when ap is crashed, AP send 0xABCD00C9  to CP, and wait 0xCACAEDED for ack. 
	if(mailboxAB == KERNEL_SEC_DUMP_AP_DEAD_ACK)
	{
		kernel_sec_set_cp_ack();
	}	
	
#endif

	if(mailboxAB == 0xabcdabcd) 
	{
		dprintk("Recieve .0xabcdabcd\n");
		boot_complete = 1;
		return IRQ_HANDLED;
	} else if (mailboxAB == 0x12341234)
	{
		dprintk("  - Received 0x12341234 from MailboxAB (DBL download OK).\n");
		return IRQ_HANDLED;
	}
	
	/* valid bit verification. @LDK@ */
	if (!(irq_mask & INT_MASK_VALID)) {
		printk(KERN_DEBUG "Invalid interrupt mask: 0x%04x\n", irq_mask);
		return IRQ_NONE;
	}

	if (*onedram_sem == 1) {
		u16 mbox;
		u32 head, tail;
		int plen = 0;
		
		READ_FROM_DPRAM(&head, DPRAM_PHONE2PDA_FORMATTED_HEAD_ADDRESS, sizeof(head));
		READ_FROM_DPRAM(&tail, DPRAM_PHONE2PDA_FORMATTED_TAIL_ADDRESS, sizeof(tail));

		if (head > tail) {
			plen = head - tail;
		} else if (tail > head) {
			plen = 4096 - head + tail;
		}
		//printk("%s: irq_mask 0x%04x to_read %d bytes.\n", __FUNCTION__, irq_mask, plen);
		mbox = check_pending_rx();
		if (irq_mask & INT_MASK_COMMAND && mbox) {
			//printk("%s: process pending rx first.\n", __FUNCTION__);
			non_command_handler(mbox);
		}


	} else {
		//printk("%s: no semaphore for AP. irq_mask 0x%04x\n", __FUNCTION__, irq_mask);
	}

	/* command or non-command? @LDK@ */
	if (irq_mask & INT_MASK_COMMAND) {
		irq_mask &= ~(INT_MASK_VALID | INT_MASK_COMMAND);
		wake_lock_timeout(&dpram_wake_lock, HZ/2);
		command_handler(irq_mask);
	}
	else {
		irq_mask &= ~INT_MASK_VALID;

		irq_mask |= check_pending_rx();
		non_command_handler(irq_mask);
	}
#if defined(CONFIG_ONEDRAM_IRQPENDING)
	mask = readl(S5PC11X_EINTPEND(eint_pend_reg(11)));
	mask &= ~(eint_irq_to_bit(11));
	writel(mask, S5PC11X_EINTPEND(eint_pend_reg(11)));
#ifdef S5PC11X_ALIVEGPIO_STORE
	readl(S5PC11X_EINTPEND(eint_pend_reg(11)));
#endif
#endif	// CONFIG_ONEDRAM_IRQPENDING

	return IRQ_HANDLED;
}

static void dpramerr_req_silent_reset(void)
{
	// modem reset state
	boot_complete = 0;
	phone_start_ok = 0;

#ifdef _ENABLE_ERROR_DEVICE
	unsigned long flags;
	char buf[DPRAM_ERR_MSG_LEN];
	phone_sync = 0;
   
	memset((void *)buf, 0, sizeof (buf));
	memcpy((void *)buf, "UNKNOWN ERROR", sizeof("UNKNOWN ERROR"));

	printk("[PHONE ERR] ->> %s\n", buf);

	local_irq_save(flags);
	memcpy(dpram_err_buf, buf, DPRAM_ERR_MSG_LEN);
	dpram_err_len = 64;
	local_irq_restore(flags);

	wake_up_interruptible(&dpram_err_wait_q);
	kill_fasync(&dpram_err_async_q, SIGIO, POLL_IN);
#endif	/* _ENABLE_ERROR_DEVICE */}
}

static void dpramerr_req_cp_upload(void)
	{
	printk("[PHONE ERROR] Set Upload Magic code and reset AP\n");        
		kernel_sec_dump_cp_handle1();
	}

static irqreturn_t phone_active_irq_handler(int irq, void *dev_id)
{

	phone_active_state = gpio_get_value(GPIO_PHONE_ACTIVE);
	
	printk(KERN_DEBUG "IRQ_PHONE_ACTIVE is Detected, level: %s\n", phone_active_state ? "HIGH" : "LOW");
	printk(KERN_DEBUG "power state : %d, boot : %d, start : %d \n",phone_power_state,boot_complete,phone_start_ok);
	
	if(!phone_active_state)
	{
#ifdef _ENABLE_ERROR_DEVICE
		if((boot_complete == 1) && (phone_start_ok == 1))
		{
			printk("Phone Error : %x \n",kernel_sec_get_debug_level());
			if(kerneldebuglevel != KERNEL_SEC_DEBUG_LEVEL_LOW)
			{
				dpramerr_req_cp_upload();
			}else
			{
				cp_silentreset_count++;
				printk("Phone silent reset : %d \n",cp_silentreset_count);
				dpramerr_req_silent_reset();
			}
		}else
		{
			printk(KERN_DEBUG "Booting is progressing, ignore \n");
		}
#endif
	}

	return IRQ_HANDLED;
}

#ifdef CONFIG_KERNEL_DEBUG_SEC

static int kernel_sec_dump_cp_handle1(void)
{
	t_kernel_sec_mmu_info mmu_info;  
	dump_on = 1;

	/*
	 * CACHE CP DUMP INFO - FileName + LinuNum
	 */
	memset((void *)cpdump_debug_file_name, 0, sizeof (cpdump_debug_file_name));

	if(*onedram_sem == 0x1)
		READ_FROM_DPRAM(cpdump_debug_file_name, DPRAM_FATAL_DISPLAY_ADDRESS,
				sizeof (cpdump_debug_file_name));

	if(cpdump_debug_file_name[0] == NULL)
		memcpy(cpdump_debug_file_name, "Unknown CP Crash", sizeof("Unknown CP Crash"));

	printk(" +--------------------------------------------+\n");
	printk(" - CP Dump Cause - \n");
	printk(" - [File Name  Line ] %s \n",cpdump_debug_file_name);
	printk(" +--------------------------------------------+\n");

	printk("[kernel_sec_dump_cp_handle1] : CP Crashed, restart and collect Dump !\n");

	kernel_sec_dump_ap_cp=1;
	kernel_sec_dump_cp_handle2();

	return 0;
}

static int kernel_sec_dump_cp_handle2(void)
{
    t_kernel_sec_mmu_info mmu_info;

    printk("[kernel_sec_dump_cp_handle2] : Configure to restart AP and collect dump on restart...\n");
        kernel_sec_set_cause_strptr(cpdump_debug_file_name, sizeof(cpdump_debug_file_name));
    kernel_sec_set_upload_magic_number();
    kernel_sec_get_mmu_reg_dump(&mmu_info);
    kernel_sec_set_upload_cause(UPLOAD_CAUSE_CP_ERROR_FATAL);
    kernel_sec_hw_reset(false);

    return 0;
}

static int kernel_sec_dump_cp_config_uploadmode(void)
{
    const u16 coredump_flag1 = 0xDEAD;
	const u16 coredump_flag2 = 0xDEAD;
    const u16 flag1,flag2;

    printk("  - [cpdump_cp_start_uploadmode] : Configure CP for UPLOAD Mode.. write 0xDEADDEAD -> MBX_AB\n");

	if(!onedram_get_semaphore(__func__))
	{
        printk("  - [cpdump_cp_start_uploadmode] : Failed to get semaphore!\n");            
		return IRQ_NONE;
	}

	if(onedram_lock_with_semaphore(__func__) < 0)
	{
        printk("  - [cpdump_cp_start_uploadmode] : Lock Failed!\n");            
		return IRQ_NONE;
	}


    // Write Magic Code to shared OneDRAM to indicate CP must boot in CoreDump mode.!
	WRITE_TO_DPRAM(DPRAM_MAGIC_CODE_ADDRESS,    &coredump_flag1, sizeof(coredump_flag1));
	WRITE_TO_DPRAM(DPRAM_ACCESS_ENABLE_ADDRESS, &coredump_flag2, sizeof(coredump_flag2));

    // Read the Shared memory to make sure data is written is properly

	READ_FROM_DPRAM((void *)&flag1, DPRAM_MAGIC_CODE_ADDRESS, sizeof(flag1));
	READ_FROM_DPRAM((void *)&flag2, DPRAM_ACCESS_ENABLE_ADDRESS, sizeof(flag2));
	printk("[cpdump_cp_start_uploadmode] MagicCode: %x AccessEnable: %x\n", flag1, flag2);

	onedram_release_lock(__func__);

	return_onedram_semaphore(__func__);

    printk("  - [cpdump_cp_start_uploadmode] : CP Upload mode config done!!\n");

    return 0;
}

#endif

/* basic functions. */
#ifdef _ENABLE_ERROR_DEVICE
static struct file_operations dpram_err_ops = {
	.owner = THIS_MODULE,
	.read = dpram_err_read,
	.fasync = dpram_err_fasync,
	.poll = dpram_err_poll,
	.llseek = no_llseek,
};
#endif	/* _ENABLE_ERROR_DEVICE */

static struct tty_operations dpram_tty_ops = {
	.open 		= dpram_tty_open,
	.close 		= dpram_tty_close,
	.write 		= dpram_tty_write,
	.write_room = dpram_tty_write_room,
	.ioctl 		= dpram_tty_ioctl,
	.chars_in_buffer = dpram_tty_chars_in_buffer,
};

#ifdef _ENABLE_ERROR_DEVICE

static void unregister_dpram_err_device(void)
{
	unregister_chrdev(DRIVER_MAJOR_NUM, DPRAM_ERR_DEVICE);
	class_destroy(dpram_class);
}

static int register_dpram_err_device(void)
{
	/* @LDK@ 1 = formatted, 2 = raw, so error device is '0' */
	struct device *dpram_err_dev_t;
	int ret = register_chrdev(DRIVER_MAJOR_NUM, DPRAM_ERR_DEVICE, &dpram_err_ops);

	if ( ret < 0 )
	{
		return ret;
	}

	dpram_class = class_create(THIS_MODULE, "err");

if (IS_ERR(dpram_class))
	{
		unregister_dpram_err_device();
		return -EFAULT;
	}

	dpram_err_dev_t = device_create(dpram_class, NULL,
			MKDEV(DRIVER_MAJOR_NUM, 0), NULL, DPRAM_ERR_DEVICE);

	if (IS_ERR(dpram_err_dev_t))
	{
		unregister_dpram_err_device();
		return -EFAULT;
	}

	return 0;
}

static void request_semaphore_timer_func(unsigned long aulong)
{
	if( requested_semaphore > 0 ) {	// cp couln't get semaphore within 500ms from last request
		printk(KERN_DEBUG "%s%02d rs=%d lock=%d\n"
				,__func__, __LINE__, 
				requested_semaphore, atomic_read(&onedram_lock));
		atomic_set(&onedram_lock, 0);
		udelay(100);
		requested_semaphore = 0;
		if(*onedram_sem == 0x1)
			*onedram_sem = 0x0;
		udelay(100);
		*onedram_mailboxBA = INT_COMMAND(INT_MASK_CMD_SMP_REP);
		printk(KERN_DEBUG "%s%02d rs=%d lock=%d\n"
				,__func__, __LINE__, 
				requested_semaphore, atomic_read(&onedram_lock));
	}
}

#endif	/* _ENABLE_ERROR_DEVICE */

static int register_dpram_driver(void)
{
	int retval = 0;

	/* @LDK@ allocate tty driver */
	dpram_tty_driver = alloc_tty_driver(MAX_INDEX);

	if (!dpram_tty_driver) {
		return -ENOMEM;
	}

	/* @LDK@ initialize tty driver */
	dpram_tty_driver->owner = THIS_MODULE;
	dpram_tty_driver->magic = TTY_DRIVER_MAGIC;
	dpram_tty_driver->driver_name = DRIVER_NAME;
	dpram_tty_driver->name = "dpram";
	dpram_tty_driver->major = DRIVER_MAJOR_NUM;
	dpram_tty_driver->minor_start = 1;
	dpram_tty_driver->num = MAX_INDEX;
	dpram_tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
	dpram_tty_driver->subtype = SERIAL_TYPE_NORMAL;
	dpram_tty_driver->flags = TTY_DRIVER_REAL_RAW;
	dpram_tty_driver->init_termios = tty_std_termios;
	dpram_tty_driver->init_termios.c_cflag =
		(B115200 | CS8 | CREAD | CLOCAL | HUPCL);

	tty_set_operations(dpram_tty_driver, &dpram_tty_ops);

	dpram_tty_driver->ttys = dpram_tty;
	dpram_tty_driver->termios = dpram_termios;
	dpram_tty_driver->termios_locked = dpram_termios_locked;

	/* @LDK@ register tty driver */
	retval = tty_register_driver(dpram_tty_driver);

	if (retval) {
		dprintk("tty_register_driver error\n");
		put_tty_driver(dpram_tty_driver);
		return retval;
	}

	return 0;
}

static void unregister_dpram_driver(void)
{
	tty_unregister_driver(dpram_tty_driver);
}

static void init_devices(void)
{
	int i;

	for (i = 0; i < MAX_INDEX; i++) {
		init_MUTEX(&dpram_table[i].serial.sem);

		dpram_table[i].serial.open_count = 0;
		dpram_table[i].serial.tty = NULL;
	}
}

static void init_hw_setting(void)
{
	/* initial pin settings - dpram driver control */
	s3c_gpio_cfgpin(GPIO_PHONE_ACTIVE, S3C_GPIO_SFN(GPIO_PHONE_ACTIVE_AF));
	s3c_gpio_setpull(GPIO_PHONE_ACTIVE, S3C_GPIO_PULL_NONE); 
	set_irq_type(IRQ_PHONE_ACTIVE, IRQ_TYPE_EDGE_BOTH);
	
	s3c_gpio_cfgpin(GPIO_nINT_ONEDRAM_AP, S3C_GPIO_SFN(GPIO_nINT_ONEDRAM_AP_AF));
	s3c_gpio_setpull(GPIO_nINT_ONEDRAM_AP, S3C_GPIO_PULL_UP); 
	set_irq_type(IRQ_INT_ONEDRAM_AP_N, IRQ_TYPE_LEVEL_LOW);

	s3c_gpio_cfgpin(GPIO_AP_FLM_RXD, S3C_GPIO_SFN(GPIO_AP_FLM_RXD_AF));
	s3c_gpio_setpull(GPIO_AP_FLM_RXD, S3C_GPIO_PULL_NONE); 

	s3c_gpio_cfgpin(GPIO_AP_FLM_TXD, S3C_GPIO_SFN(GPIO_AP_FLM_TXD_AF));
	s3c_gpio_setpull(GPIO_AP_FLM_TXD, S3C_GPIO_PULL_NONE); 

	if (gpio_is_valid(GPIO_PHONE_ON)) {
		if (gpio_request(GPIO_PHONE_ON, "GPJ1"))
			printk(KERN_ERR "Filed to request GPIO_PHONE_ON!\n");
		gpio_direction_output(GPIO_PHONE_ON, GPIO_LEVEL_LOW);
	}
	s3c_gpio_setpull(GPIO_PHONE_ON, S3C_GPIO_PULL_NONE); 


	if (gpio_is_valid(GPIO_CP_RST)) {
		if (gpio_request(GPIO_CP_RST, "GPH3"))
			printk(KERN_ERR "Filed to request GPIO_CP_RST!\n");
		gpio_direction_output(GPIO_CP_RST, GPIO_LEVEL_LOW);
	}
	s3c_gpio_setpull(GPIO_CP_RST, S3C_GPIO_PULL_NONE); 

	if (gpio_is_valid(GPIO_PDA_ACTIVE)) {
		if (gpio_request(GPIO_PDA_ACTIVE, "MP03"))
			printk(KERN_ERR "Filed to request GPIO_PDA_ACTIVE!\n");
		gpio_direction_output(GPIO_PDA_ACTIVE, GPIO_LEVEL_HIGH);
	}
	s3c_gpio_setpull(GPIO_PDA_ACTIVE, S3C_GPIO_PULL_NONE); 


    if (HWREV<=3)
	{
        s3c_gpio_setpull(GPIO_USIM_BOOT, S3C_GPIO_PULL_NONE);
    	if (gpio_is_valid(GPIO_FLM_SEL_R03)) {
    		if (gpio_request(GPIO_FLM_SEL_R03, "GPJ2"))
    			printk(KERN_ERR "Filed to request GPIO_FLM_SEL!\n");
        		gpio_direction_output(GPIO_FLM_SEL_R03, GPIO_LEVEL_LOW);
        	}
        }
        else
    	{
            if (gpio_is_valid(GPIO_FLM_SEL_R04)) {
        		if (gpio_request(GPIO_FLM_SEL_R04, "GPG3"))
    			printk(KERN_ERR "Filed to request GPIO_FLM_SEL!\n");
        		gpio_direction_output(GPIO_FLM_SEL_R04, GPIO_LEVEL_LOW);
        	}
	}

		if (gpio_is_valid(GPIO_USIM_BOOT)) {
		if (gpio_request(GPIO_USIM_BOOT, "GPJ2"))
			printk(KERN_ERR "Filed to request GPIO_USIM_BOOT!\n");
		gpio_direction_output(GPIO_USIM_BOOT, GPIO_LEVEL_LOW);
	}
	s3c_gpio_setpull(GPIO_USIM_BOOT, S3C_GPIO_PULL_NONE);
}

static void kill_tasklets(void)
{
	tasklet_kill(&fmt_res_ack_tasklet);
	tasklet_kill(&raw_res_ack_tasklet);
	tasklet_kill(&rfs_res_ack_tasklet);

	tasklet_kill(&fmt_send_tasklet);
	tasklet_kill(&raw_send_tasklet);
	tasklet_kill(&rfs_send_tasklet);
}

static int register_interrupt_handler(void)
{

	unsigned int dpram_irq, phone_active_irq;
	int retval = 0;
	
	dpram_irq = IRQ_INT_ONEDRAM_AP_N;
	phone_active_irq = IRQ_PHONE_ACTIVE;

	/* @LDK@ interrupt area read - pin level will be driven high. */
	dpram_clear();

	/* @LDK@ dpram interrupt */
	retval = request_irq(dpram_irq, dpram_irq_handler, IRQF_DISABLED, "dpram irq", NULL);

	if (retval) {
		dprintk("DPRAM interrupt handler failed.\n");
		unregister_dpram_driver();
		return -1;
	}

	/* @LDK@ phone active interrupt */
	retval = request_irq(phone_active_irq, phone_active_irq_handler, IRQF_DISABLED, "Phone Active", NULL);

	if (retval) {
		dprintk("Phone active interrupt handler failed.\n");
		free_irq(phone_active_irq, NULL);
		unregister_dpram_driver();
		return -1;
	}

	return 0;
}

static void check_miss_interrupt(void)
{
	unsigned long flags;

	if (gpio_get_value(GPIO_PHONE_ACTIVE) &&
			(!gpio_get_value(GPIO_nINT_ONEDRAM_AP))) {
		dprintk("there is a missed interrupt. try to read it!\n");

		if (*onedram_sem != 0x1) {
			printk(KERN_DEBUG "[OneDRAM] (%s) semaphore: %d\n", __func__, *onedram_sem);
			onedram_get_semaphore(__func__);
		}

		local_irq_save(flags);
		dpram_irq_handler(IRQ_INT_ONEDRAM_AP_N, NULL);
		local_irq_restore(flags);
	}

}

static int dpram_suspend(struct platform_device *dev, pm_message_t state)
{
	if(receive_mms)
	{
		receive_mms=0;
	}
	return 0;
}

static int dpram_resume(struct platform_device *dev)
{
	check_miss_interrupt();
	return 0;
}


static int onedram_rfs_open(struct inode *inode, struct file *file)
{
	int ret=0;
	int i=0;
	dprintk("onedram_rfs() entered\n");
	return 0;
}

static int rfs_region_pagefault(struct vm_area_struct *vma,
				struct vm_fault *vmf)
{
	//dprintk("rfs_region_pagefault l\n");
	
	if (!onedram_rfs_data.buffer)
	{
		printk(KERN_DEBUG "rfs_buffer is null\n");
		return VM_FAULT_SIGBUS;
	}

	vmf->page = vmalloc_to_page(onedram_rfs_data.buffer + (vmf->pgoff << PAGE_SHIFT));
	get_page(vmf->page);
	//dprintk("rfs_region_pagefault END l\n");
	return 0;
}

struct vm_operations_struct rfs_vm_ops =
{
	.fault = rfs_region_pagefault,
};

static int onedram_rfs_mmap(struct file *file, struct vm_area_struct *vma)
{

	dprintk("onedram_rfs_mmap l\n");
	vma->vm_flags |= VM_RESERVED;
	vma->vm_ops = &rfs_vm_ops;
	return 0;
}

static int 
onedram_rfs_ioctl(struct inode *inode, struct file *file,
	      unsigned int cmd, unsigned long arg)
{

	switch (cmd) {
		case ONEDRAM_RFS_BUFER_FREE:
			if(onedram_rfs_data.data_in_buffer)
			{
				onedram_rfs_data.data_in_buffer = 0;
				dprintk("onedram_rfs_ioctl buffer free\n");
			}
			if(onedram_rfs_data.data_in_onedram)
			{
				wake_lock_timeout(&dpram_wake_lock, 4*HZ);
				dpram_tasklet_data[RFS_INDEX].device = &dpram_table[RFS_INDEX];
				dpram_tasklet_data[RFS_INDEX].non_cmd = INT_MASK_SEND_RFS;
				rfs_send_tasklet.data = (unsigned long)&dpram_tasklet_data[RFS_INDEX];
				/* @LDK@ raw buffer op. -> soft irq level. */
				tasklet_schedule(&rfs_send_tasklet);
				dprintk("onedram_rfs_ioctl data_in_onedram \n");
			}
			break;
		
		case ONEDRAM_RFS_BUFER_TEST:
			dprintk("onedram_rfs_ioctl buffer DATA %s\n", onedram_rfs_data.buffer);
			break;
			
		default:
			return -ENOTTY;
	}
	dprintk("onedram_rfs_ioctl entered\n");
	return 0;
}

static int onedram_rfs_release(struct inode *inode, struct file *file)
{
	dprintk(" onedram_rfs_release() entered\n");
	return 0;
}

static struct file_operations onedram_rfs_fops = {
	.owner = THIS_MODULE,
	.open = onedram_rfs_open,
	.release = onedram_rfs_release,
	.ioctl = onedram_rfs_ioctl,
	.mmap = onedram_rfs_mmap,
};


static struct miscdevice onedram_rfs_device = {
	.minor = ONEDRAM_RFS_MINOR,
	.name = "onedram_rfs",
	.fops = &onedram_rfs_fops,
};

static int __devinit dpram_probe(struct platform_device *dev)
{
	int retval;

	init_waitqueue_head(&dpram_wait);
	
	onedram_rfs_data.buffer = vmalloc(SHARE_MEM_SIZE);
	if(!onedram_rfs_data.buffer)
	{
		printk(KERN_DEBUG "Cannot allocate onedram_rfs_data.buffer\n");
	}
	retval = misc_register(&onedram_rfs_device);
	
	if (retval) {
		printk(KERN_DEBUG "onedram_rfs_device Cannot register miscdev ret: %d \n",retval);
		return -1;
	}
	printk("Onedram rfs register success\n");
	
	/* @LDK@ register dpram (tty) driver */
	retval = register_dpram_driver();
	if (retval) {
		dprintk("Failed to register dpram (tty) driver.\n");
		return -1;
	}

#ifdef _ENABLE_ERROR_DEVICE
	/* @LDK@ register dpram error device */
	retval = register_dpram_err_device();
	if (retval) {
		dprintk("Failed to register dpram error device.\n");

		unregister_dpram_driver();
		return -1;
	}

	memset((void *)dpram_err_buf, '\0', sizeof dpram_err_buf);
#endif /* _ENABLE_ERROR_DEVICE */

	setup_timer(&request_semaphore_timer, request_semaphore_timer_func, 0);
	
	/* @LDK@ H/W setting */
	init_hw_setting();

	dpram_shared_bank_remap();

	/* @LDK@ register interrupt handler */
	if ((retval = register_interrupt_handler()) < 0) {
		return -1;
	}

	/* @LDK@ initialize device table */
	init_devices();

#ifdef CONFIG_PROC_FS
	create_proc_read_entry(DRIVER_PROC_ENTRY, 0, 0, dpram_read_proc, NULL);
#endif	/* CONFIG_PROC_FS */

	/* @LDK@ check out missing interrupt from the phone */
//	check_miss_interrupt();
	gpio_set_value(GPIO_PDA_ACTIVE, GPIO_LEVEL_HIGH);

	return 0;
}

static int __devexit dpram_remove(struct platform_device *dev)
{
	/* @LDK@ unregister dpram (tty) driver */
	unregister_dpram_driver();

	/* @LDK@ unregister dpram error device */
#ifdef _ENABLE_ERROR_DEVICE
	unregister_dpram_err_device();
#endif
	/* @LDK@ unregister irq handler */
	
	free_irq(IRQ_INT_ONEDRAM_AP_N, NULL);
	free_irq(IRQ_PHONE_ACTIVE, NULL);

	kill_tasklets();

	return 0;
}

static int dpram_shutdown(struct platform_device *dev)
{
	int phone_active;
	unsigned int modemoff_count=0;
	int ret = 0;

	printk(KERN_DEBUG "dpram shutdown\n");
	ret = del_timer(&request_semaphore_timer);

	unregister_dpram_driver();
	unregister_dpram_err_device();
	
	free_irq(IRQ_INT_ONEDRAM_AP_N, NULL);
	free_irq(IRQ_PHONE_ACTIVE, NULL);

	kill_tasklets();

	if(phone_shutdown == 1)
	{
	    gpio_set_value(GPIO_PHONE_ON, 0);
		printk(KERN_DEBUG "Waiting modem power off...\n");
    
		while(1)
		{
	    phone_active = gpio_get_value(GPIO_PHONE_ACTIVE);
    
	    if(phone_active)
	    {
		    modemoff_count++;
		    if(modemoff_count > MODEMOFF_TIMEOUT)
		    {
		    	printk(KERN_DEBUG "modem power off fail, kernel handle it \n");
				break;
		    }
			mdelay(1000);
			}else
			{
				printk(KERN_DEBUG "modem power off success \n");
				break;
			}
		}

		printk(KERN_DEBUG "end modem power off %d...\n",modemoff_count);
	}else
	{
		printk(KERN_DEBUG "It's not shutdown case \n");
	}

	return 0;
}

static struct platform_driver platform_dpram_driver = {
	.probe		= dpram_probe,
	.remove		= __devexit_p(dpram_remove),
	.shutdown	= dpram_shutdown,
	.suspend	= dpram_suspend,
	.resume 	= dpram_resume,
	.driver	= {
		.name	= "dpram-device",
	},
};

/* init & cleanup. */
static int __init dpram_init(void)
{
	wake_lock_init(&dpram_wake_lock, WAKE_LOCK_SUSPEND, "DPRAM");
	return platform_driver_register(&platform_dpram_driver);
}

static void __exit dpram_exit(void)
{
	wake_lock_destroy(&dpram_wake_lock);
	platform_driver_unregister(&platform_dpram_driver);
}

module_init(dpram_init);
module_exit(dpram_exit);

MODULE_AUTHOR("SAMSUNG ELECTRONICS CO., LTD");
MODULE_DESCRIPTION("Onedram Device Driver.");
MODULE_LICENSE("GPL");
