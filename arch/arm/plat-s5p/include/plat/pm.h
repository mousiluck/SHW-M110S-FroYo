/* linux/arch/arm/plat-s5p/include/plat/pm.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com/
 *
 * Based on arch/arm/plat-s3c/include/plat/pm.h,
 * Copyright 2008-2009 Simtec Electronics Ben Dooks <ben@simtec.co.uk>
 * S5P series device definition for power management
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifdef CONFIG_S5P_LPAUDIO
#include <linux/wakelock.h>
#include <mach/cpuidle.h>
#endif /* CONFIG_S5P_LPAUDIO */

#ifdef CONFIG_PM

extern __init int s3c_pm_init(void);

#else

static inline int s3c_pm_init(void)
{
	return 0;
}
#endif

#define INTR_MODE_EDGE_FALLING 	0x2
#define INTR_MODE_EDGE_RISING 	0x3
#define INTR_MODE_EDGE_BOTH 	0x4

/*parsing wakeup source*/
/*wakeup_stat register bit*/
#define WAKEUP_BIT_EINT				(1 << 0)
#define WAKEUP_BIT_RTC_ALARM		(1 << 1)
#define WAKEUP_BIT_RTC_TICK			(1 << 2)
/*eint0 pending*/
#define EINTPEND0_BIT_PROX			(1 << 2)
#define EINTPEND0_BIT_EARJACK		(1 << 6)
#define EINTPEND0_BIT_PMIC			(1 << 7)
/*eint1 pending*/
#define EINTPEND1_BIT_FUELGAUGE		(1 << 0)
#define EINTPEND1_BIT_ONEDRAM		(1 << 3)
/*eint2 pending*/
#define EINTPEND2_BIT_WLAN			(1 << 4)
#define EINTPEND2_BIT_BT			(1 << 5)
#define EINTPEND2_BIT_PWRBUTTON		(1 << 6)
#define EINTPEND2_BIT_MICROUSB		(1 << 7)
/*eint3 pending*/
#define EINTPEND3_BIT_OK			(1 << 0)
#define EINTPEND3_BIT_VOLDN			(1 << 1)
#define EINTPEND3_BIT_VOLUP			(1 << 2)
#define EINTPEND3_BIT_SDDET			(1 << 4)
#define EINTPEND3_BIT_EARSENDEND	(1 << 6)

/*wakeup source define*/
#define	WAKEUP_SRC_RTC_ALARM		(1 << 0)
#define WAKEUP_SRC_RTC_TICK			(1 << 1)
#define WAKEUP_SRC_PROX				(1 << 2)
#define WAKEUP_SRC_EARJACK			(1 << 3)
#define	WAKEUP_SRC_PMIC				(1 << 4)
#define WAKEUP_SRC_FUELGAUGE		(1 << 5)
#define WAKEUP_SRC_ONEDRAM			(1 << 6)
#define WAKEUP_SRC_WLAN				(1 << 7)
#define	WAKEUP_SRC_BT				(1 << 8)
#define WAKEUP_SRC_PWRBUTTON		(1 << 9)
#define WAKEUP_SRC_MICROUSB			(1 << 10)
#define WAKEUP_SRC_HOME				(1 << 11)
#define	WAKEUP_SRC_VOLDN			(1 << 12)
#define WAKEUP_SRC_VOLUP			(1 << 13)
#define WAKEUP_SRC_SDDET			(1 << 14)
#define WAKEUP_SRC_EARSENDEND		(1 << 15)


#ifdef CONFIG_S3C2410_PM_DEBUG
extern void printascii(const char *);
#endif

extern void s3c_pm_dbg(const char *fmt, ...);

/* configuration for the IRQ mask over sleep */
extern unsigned long s3c_irqwake_intmask;
extern unsigned long s3c_irqwake_eintmask;

/* IRQ masks for IRQs allowed to go to sleep (see irq.c) */
extern unsigned long s3c_irqwake_intallow;
extern unsigned long s3c_irqwake_eintallow;

/* per-cpu sleep functions */

extern void (*pm_cpu_prep)(void);
extern void (*pm_cpu_sleep)(void);

/* Flags for PM Control */

extern unsigned long s3c_pm_flags;

extern unsigned char pm_uart_udivslot;  /* true to save UART UDIVSLOT */

/* from sleep.S */

extern int  s3c_cpu_save(unsigned long *saveblk);
extern void s3c_cpu_resume(void);

extern void s3c2410_cpu_suspend(void);

extern unsigned long s3c_sleep_save_phys;

#define SLEEP_MODE 0
#define IDLE2_MODE 1

/* sleep save info */

/**
 * struct sleep_save - save information for shared peripherals.
 * @reg: Pointer to the register to save.
 * @val: Holder for the value saved from reg.
 *
 * This describes a list of registers which is used by the pm core and
 * other subsystem to save and restore register values over suspend.
 */
struct sleep_save {
	void __iomem	*reg;
	unsigned long	val;
};

#define SAVE_ITEM(x) \
	{ .reg = (x) }

/**
 * struct pm_uart_save - save block for core UART
 * @ulcon: Save value for S3C2410_ULCON
 * @ucon: Save value for S3C2410_UCON
 * @ufcon: Save value for S3C2410_UFCON
 * @umcon: Save value for S3C2410_UMCON
 * @ubrdiv: Save value for S3C2410_UBRDIV
 *
 * Save block for UART registers to be held over sleep and restored if they
 * are needed (say by debug).
*/
struct pm_uart_save {
	u32	ulcon;
	u32	ucon;
	u32	ufcon;
	u32	umcon;
	u32	ubrdiv;
	u32	udivslot;
};

/* helper functions to save/restore lists of registers. */

extern int s5p_irq_suspend(struct sys_device *dev, pm_message_t state);
extern int s5p_irq_resume(struct sys_device *dev);

/* PM debug functions */

#ifdef CONFIG_S3C2410_PM_DEBUG
/**
 * s3c_pm_dbg() - low level debug function for use in suspend/resume.
 * @msg: The message to print.
 *
 * This function is used mainly to debug the resume process before the system
 * can rely on printk/console output. It uses the low-level debugging output
 * routine printascii() to do its work.
 */
extern void s3c_pm_dbg(const char *msg, ...);

#define S3C_PMDBG(fmt...) s3c_pm_dbg(fmt)
#else
#define S3C_PMDBG(fmt...) printk(KERN_INFO fmt)
#endif

#ifdef CONFIG_S3C_PM_DEBUG_LED_SMDK
/**
 * s3c_pm_debug_smdkled() - Debug PM suspend/resume via SMDK Board LEDs
 * @set: set bits for the state of the LEDs
 * @clear: clear bits for the state of the LEDs.
 */
extern void s3c_pm_debug_smdkled(u32 set, u32 clear);

#else
static inline void s3c_pm_debug_smdkled(u32 set, u32 clear) { }
#endif /* CONFIG_S3C_PM_DEBUG_LED_SMDK */

/* suspend memory checking */

#ifdef CONFIG_S3C2410_PM_CHECK
extern void s3c_pm_check_prepare(void);
extern void s3c_pm_check_restore(void);
extern void s3c_pm_check_cleanup(void);
extern void s3c_pm_check_store(void);
#else
#define s3c_pm_check_prepare() do { } while (0)
#define s3c_pm_check_restore() do { } while (0)
#define s3c_pm_check_cleanup() do { } while (0)
#define s3c_pm_check_store()   do { } while (0)
#endif

extern void s3c_pm_do_save(struct sleep_save *ptr, int count);
extern void s3c_pm_do_restore(struct sleep_save *ptr, int count);
extern void s3c_pm_do_restore_core(struct sleep_save *ptr, int count);

#ifdef CONFIG_PM
extern int s3c_irqext_wake(unsigned int irqno, unsigned int state);
extern int s3c24xx_irq_suspend(struct sys_device *dev, pm_message_t state);
extern int s3c24xx_irq_resume(struct sys_device *dev);
#else
#define s3c_irqext_wake NULL
#define s3c24xx_irq_suspend NULL
#define s3c24xx_irq_resume  NULL
#endif

/**
 * s3c_pm_configure_extint() - ensure pins are correctly set for IRQ
 *
 * Setup all the necessary GPIO pins for waking the system on external
 * interrupt.
 */
extern void s3c_pm_configure_extint(void);

/**
 * s3c_pm_restore_gpios() - restore the state of the gpios after sleep.
 *
 * Restore the state of the GPIO pins after sleep, which may involve ensuring
 * that we do not glitch the state of the pins from that the bootloader's
 * resume code has done.
*/
extern void s3c_pm_restore_gpios(void);

/**
 * s3c_pm_save_gpios() - save the state of the GPIOs for restoring after sleep.
 *
 * Save the GPIO states for resotration on resume. See s3c_pm_restore_gpios().
 */
extern void s3c_pm_save_gpios(void);

/**
 * s3c_pm_cb_flushcache - callback for assembly code
 *
 * Callback to issue flush_cache_all() as this call is
 * not a directly callable object.
 */
extern void s3c_pm_cb_flushcache(void);

extern void s3c_pm_save_core(void);
extern void s3c_pm_restore_core(void);
extern void s5pc11x_pm_do_dump(void);
extern unsigned long s5pc11x_pm_get_wakeup_src(void);
extern void s5pc11x_pm_clear_wakeup_src(void);

#if defined(CONFIG_MACH_S5PC110_P1)
enum ENUM_PM_WAKEUP_STAT
{
	PM_WAKEUP_NONE,
	PM_POWER_KEY,
	PM_VOLUME_UP,
	PM_VOLUME_DOWN
};

extern int s3c_pm_get_wakeup_stat(void);
extern void s3c_pm_clear_wakeup_stat(void);
#endif // CONFIG_MACH_S5PC110_P1

