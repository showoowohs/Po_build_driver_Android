#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <mach/mt_gpio.h>
#include <linux/kpd.h>
//#include <linux/kmod.h>
#include <linux/file.h>

#define PROC_GPIO
#define PROC_KBD
#define PROC_ETH0
#define PROC_USBStatus
#define PROC_MTKIMEI

#ifdef PROC_ETH0
static struct proc_dir_entry *eth0_file;
static int proc_read_eth0(char *page, char **start, off_t off, int count, int *eof, void *data);
static int proc_write_eth0(struct file *file, const char *buffer, unsigned long count, void *data);
static int eth0_help = 0;
static int eth0_Mac = 0;
static char ETH0_HELP[] =
	"1. echo help > /proc/eth0\n"
	"2. echo ShowMac > /proc/eth0"
	"\t(not function)\n"
	"3. echo FlashMac > /proc/eth0"
	"\t(can flash LAN Mac)\n"
	"4. echo ClearMac > /proc/eth0"
	"\t(can clear LAN Mac)\n";

static char ETH0_Mac[1024] = "";

mm_segment_t oldfs;

struct file *openFile(char *path,int flag,int mode) 
{ 
	struct file *fp; 

	fp=filp_open(path, flag, 0); 
	if (fp) return fp; 
	else return NULL; 
} 

int readFile(struct file *fp,char *buf,int readlen) 
{ 
	if (fp->f_op && fp->f_op->read) 
		return fp->f_op->read(fp,buf,readlen, &fp->f_pos); 
	else 
		return -1; 
} 

int closeFile(struct file *fp) 
{ 
	filp_close(fp,NULL); 
	return 0; 
} 

void initKernelEnv(void) 
{ 
	oldfs = get_fs(); 
	set_fs(KERNEL_DS); 
} 

static int proc_read_eth0(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char *p = page;
	int len = 0;

	if (eth0_help == 1) {
		eth0_Mac = 0;
		eth0_help = 0;
		p += sprintf (p, ETH0_HELP);
		IMT8_printk("[Po add] %s %s\n", __func__, p);
	}else if (eth0_Mac == 1){
		eth0_Mac = 0;
		eth0_help = 0;
		p += sprintf (p, ETH0_Mac);
		IMT8_printk("[Po add] %s %s\n", __func__, p);
	}else{
		p += sprintf (p, "not thing\n");
		IMT8_printk("[Po add] %s %s\n", __func__, p);
	}

	*start = page + off;

	len = p - page;
	if (len <= off + count) *eof = 1;

	len -= off;
	if (len > count) len = count;
	if (len < 0) len = 0;
	return len;
}

//echo 69 1 > /proc/eth0
static int proc_write_eth0(struct file *file, const char *buffer, unsigned long count, void *data)
{
	char eth0_data[50];

	if (count > 50){
		return -EINVAL;
	}else if (count == 0) {
		return 0;
	}

	if(copy_from_user(eth0_data, buffer, count)){
		return -EFAULT;
	}

	if (eth0_data[0] == 'h' || eth0_data[0] == 'H') { // help
		eth0_help = 1;
		IMT8_printk("[Po add] %s Help\n", __func__);
	}else if ((strncmp(eth0_data, "ShowMac", 7) == 0) || (eth0_data[0] == 'S' )) {

		IMT8_printk("[Po add] %s ShowMac\n", __func__);
		/*
		char * envp[] = { "HOME=/", NULL };
		char * argv[] = { "/system/ethtool/ee9wj", "eth0", ">", "/system/aaa", NULL};
		int ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC);
		*/

		// Read /system/MacStatus.txt
		char buf[1024]; 
		struct file *fp; 
		int ret; 

		initKernelEnv(); 
		fp=openFile("/system/MacStatus.txt",O_RDONLY,0); 
		if (fp!=NULL){ 
			memset(buf,0,1024); 
			if ((ret=readFile(fp,buf,1024))>0){ 
				//IMT8_printk("[Po add] buf:%s\n",buf); 

				// strsep
				// 0x0000:              00 60 6e 10 00 1 51 55 46 0a 21 96 00 00.......
				char* const delim = ":";  
				char *token, *cur = buf;  
				char * tmp[1024];
				while (token = strsep(&cur, delim)) {  
					//IMT8_printk("[Po add] %s\n", token);  
					strcpy(ETH0_Mac, token);
				}  
				eth0_Mac=1;
			}else{
				IMT8_printk("[Po add] read file error %d\n",ret); 
			}
			closeFile(fp); 
		} 
		set_fs(oldfs); 

		IMT8_printk("[Po add] %s ShowMac %d\n", __func__, ret);
		IMT8_printk("[Po add] %s ShowMac ETH0_Mac=%s\n", __func__, ETH0_Mac);

	}else if (strncmp(eth0_data, "FlashMacSD", 10) == 0) {

		char * envp[] = { "HOME=/", NULL };
		char * argv[] = { "/system/ethtool/ee9wj", "eth0", "/sdcard/Flash_Ethernet_Mac/eedata.txt", NULL};
		int ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC);
		if(ret == 0){
			IMT8_printk("[Po add] %s FlashMac success\n", __func__);
		}else{
			IMT8_printk("[Po add] %s FlashMac error, ret->%d\n", __func__, ret);
		}

	}else if (strncmp(eth0_data, "FlashMac", 8) == 0) {

		char * envp[] = { "HOME=/", NULL };
		char * argv[] = { "/system/ethtool/ee9wj", "eth0", "/system/ethtool/eedata.txt", NULL};
		int ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC);
		if(ret == 0){
			IMT8_printk("[Po add] %s FlashMac success(/sdcard/Flash_Ethernet_Mac/eedata.txt)\n", __func__);
		}else{
			IMT8_printk("[Po add] %s FlashMac error(/sdcard/Flash_Ethernet_Mac/eedata.txt), ret->%d\n", __func__, ret);
		}

	}else if (strncmp(eth0_data, "ClearMac", 8) == 0) {

		char * envp[] = { "HOME=/", NULL };
		char * argv[] = { "/system/ethtool/ee9wj", "eth0", "/system/ethtool/eeclear.txt", NULL};
		int ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC);
		if(ret == 0){
			IMT8_printk("[Po add] %s ClearMac success\n", __func__);
		}else{
			IMT8_printk("[Po add] %s ClearMac error ret->%d\n", __func__, ret);
		}

        }

	return count;
}
#endif

#ifdef PROC_KBD
extern int had_read, hotkeyIdx, kpd_pressed, send_keycode;
extern u16 kpd_func_keymap[];
extern void imobile_init_kpd_func_keymap(u16 k1, u16 k2, u16 k3, u16 k4, u16 k5);
static int kpd_help = 0;

static struct proc_dir_entry *kpd_file;
static int proc_read_kpd(
                char *page, char **start, off_t off, int count, int *eof, void *data);
static int proc_write_kpd(
                struct file *file, const char *buffer, unsigned long count, void *data);

static char HELP[] =
  "1. echo help > /proc/kpd\n"
  "2. echo {disable|enable} HOTKEY > /proc/kpd\n"
  "\t  where HOTKEY is in [T1234], for example to disable T key or enable key-1 :\n"
  "\t    echo disable T > /proc/kpd; echo enable 1 > /proc/kpd\n"
  "3. echo {QUIET|NOTQUIET} > /proc/kpd\n"
  "\t  QUIET will do not send key event, NOTQUIET for act as normal KEY\n"
  "\t  please refer to kpd-service\n"
  "4. echo remap TKEY KEY1 KEY2 KEY3 KEY4 > /proc/kpd\n"
  "\t  where TKEY,KEY[1-4] are linux key code, refer to linux/input.h\n"
  "5. echo reset > /proc/kpd\n"
  "\t  will reset keymap to default\n"
  "6. echo FUNC_ENABLE > /proc/kpd\n"
  "\t  will reset keymap to T-Key mode, with HOME, F13, F14, F15, F16\n"
  "7. cat /proc/kpd\n"
  "\t  will show the last key event status in\n'HOTKEY KEYCODE PRESSED READ'\n"
  "\t    HOTKEY: positive for enabled, negative for disabled\n"
  "\t    PRESSED: indicate if the button pressed or release\n"
  "\t    READ   : 1 for had been read, reset by new key hit\n";

static int proc_read_kpd(
		char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char *p = page;
	int len = 0;

	IMT8_printk("[kpd] proc_read_kpd(%d)\n", hotkeyIdx);
	if (kpd_help == 1) {
		kpd_help = 0;
		p += sprintf (p, HELP);
		for (hotkeyIdx=0; hotkeyIdx<5; hotkeyIdx++) {
		  p += sprintf(p, "\t%d\t%d\t%d\t%d\n",
			hotkeyIdx, kpd_func_keymap[hotkeyIdx], kpd_pressed, had_read);
		}
	}

	else if (hotkeyIdx >= 0 && hotkeyIdx <= 4) {
		p += sprintf (p, "%d %d %d %d\n",
			hotkeyIdx, kpd_func_keymap[hotkeyIdx], kpd_pressed, had_read);
		had_read++;
	}

	*start = page + off;

	len = p - page;
	if (len <= off + count) *eof = 1;

	len -= off;
	if (len > count) len = count;
	if (len < 0) len = 0;
	return len;
}

// wade, procfs $5, echo 69 1 > /proc/kpd
static int proc_write_kpd(
		struct file *file, const char *buffer, unsigned long count, void *data)
{
	char kpd_data[50];
	u16 Tc, kc1, kc2, kc3, kc4;
	char key;

	if (count > 50) return -EINVAL;
	else if (count == 0) return 0;

	if(copy_from_user(kpd_data, buffer, count))
		return -EFAULT;

	if (kpd_data[0] == 'h' || kpd_data[0] == 'H') { // help
		kpd_help = 1;
	}
	else if (strncmp(kpd_data, "reset", 5) == 0) {
		imobile_init_kpd_func_keymap(0, KEY_HOME, KEY_BACK, KEY_VOLUMEUP, KEY_VOLUMEDOWN);
	}
	else if (strncmp(kpd_data, "FUNC_KEY", 8) == 0) {
		imobile_init_kpd_func_keymap(KEY_HOME, KEY_F13, KEY_F14, KEY_F15, KEY_F16);
	}
	else if (strncmp(kpd_data, "NOTQUIET", 8) == 0) {
          send_keycode = 1;
        }
	else if (strncmp(kpd_data, "QUIET", 5) == 0) {
          send_keycode = 0;
        }
	else if (sscanf (kpd_data, "enable %c", &key) == 1) {
		if (key == 'T') kc1 = 0;
		else if (key >= '1' && key <= '4') kc1 = key - '0';
		else kc1 = -1;
		if (kc1 >= 0 && kpd_func_keymap[kc1] < 0)
			 kpd_func_keymap[kc1] = -kpd_func_keymap[kc1];
	}
	else if (sscanf (kpd_data, "disable %c", &key) == 1) {
		if (key == 'T') kc1 = 0;
		else if (key >= '1' && key <= '4') kc1 = key - '0';
		else kc1 = -1;
		if (kc1 >= 0 && kpd_func_keymap[kc1] > 0)
			 kpd_func_keymap[kc1] = -kpd_func_keymap[kc1];
	}
	else if ((sscanf (kpd_data, "remap %u %u %u %u %u", &Tc, &kc1, &kc2, &kc3, &kc4) == 5)) {
		imobile_init_kpd_func_keymap(Tc, kc1, kc2, kc3, kc4);
	}

	return count;
}
#endif

#ifdef PROC_GPIO
static int gpio_to_read = -1;
static struct proc_dir_entry *gpio_file;
static int proc_read_gpio(
  char *page, char **start, off_t off, int count, int *eof, void *data);
static int proc_write_gpio(
  struct file *file, const char *buffer, unsigned long count, void *data);

int procfs_get_gpio(int gpio)
{
  int dir = mt_get_gpio_dir(gpio);
  int gpio_value;

  if (dir == GPIO_DIR_IN) 
    gpio_value = mt_get_gpio_in(gpio);
  else
    gpio_value = mt_get_gpio_out(gpio);
  return gpio_value;
}

// procfs #5
// cat /proc/gpio
static int proc_read_gpio(
  char *page, char **start, off_t off, int count, int *eof, void *data)
{
  char *p = page;
  int len = 0;

  if (gpio_to_read < 0) {
    p += sprintf(p,
      "1. write -1 to select gpio#, usage as:\n"
	"\techo -1 GPIO# > /proc/gpio\n"
      	"\tfor example, select GPIO67 to be read:\n"
      	"\techo -1 67 > /proc/gpio\n"
      "2. read from /proc/gpio again, such as:\n"
      	"\tcat /proc/gpio\n"
      "3. write usage normally is:\n"
      	"\techo GPIO# LEVEL [GPIO_MODE [GPIO_DIR]] > /proc/gpio\n"
      	"\twhere LEVEL could be [0,1,-1], -1 to switch 1 from 0 or 0 from 1\n"
      	"\tfor example:\n"
      	"\t  echo 67 1 > /proc/gpio\n"
      	"\t  echo 67 1 0 > /proc/gpio\n"
      	"\twhere mode could be 0..7,\n"
      	"\t  and dir could be 0(IN) or 1(OUT)\n");
  }
  else {
    int gpio_dir = mt_get_gpio_dir(gpio_to_read);
    int gpio_mode = mt_get_gpio_mode(gpio_to_read);
    int gpio_value = procfs_get_gpio(gpio_to_read);

    //IMT8_printk("%s(%d, %d, %d, %d)\n", __func__, gpio_to_read, gpio_value, gpio_mode, gpio_dir);
    p += sprintf(p, "GPIO (Level,Mode,Dir): (%d,%d,%d)\n",
                 gpio_value, gpio_mode, gpio_dir);
  }

  *start = page + off;

  len = p - page;
  if (len <= off + count) *eof = 1;

  len -= off;
  if (len > count) len = count;
  if (len < 0) len = 0;
  return len;
}

void procfs_set_gpio(int gpio, int level, int mode, int dir)
{
  int gpio_dir;

  //IMT8_printk("%s(%d, %d, %d, %d)\n", __func__, gpio, level, mode, dir);
  if (dir == -1) gpio_dir = mt_get_gpio_dir(gpio);
  else gpio_dir = dir;

  if (mode >= GPIO_MODE_00 && mode < GPIO_MODE_MAX) { // Valid mode
    mt_set_gpio_mode (gpio, mode);
  }

  mt_set_gpio_dir(gpio, 1);
  if (level)
    mt_set_gpio_out(gpio, GPIO_OUT_ONE);
  else
    mt_set_gpio_out(gpio, GPIO_OUT_ZERO);

  mt_set_gpio_dir(gpio, gpio_dir);
 }
 
// echo 69 1 > /proc/gpio
static int proc_write_gpio(
  struct file *file, const char *buffer, unsigned long count, void *data)
{
  char gpio_data[256];

  //IMT8_printk("%s(%lu)\n", __func__, count);
  if (count > 255) return -EINVAL;
  else if (count == 0) return 0;

  if(copy_from_user(gpio_data, buffer, count))
    return -EFAULT;

  int gpio, level, mode=-1, dir=-1;
  if (sscanf (gpio_data, "%d %d %d %d", &gpio, &level, &mode, &dir) != 4 &&
      sscanf (gpio_data, "%d %d %d", &gpio, &level, &mode) !=3 &&
      sscanf (gpio_data, "%d %d", &gpio, &level) != 2)
  {
    //IMT8_printk("usage: echo gpio level [mode [dir]] > /proc/gpio\n");
    return -EINVAL;
  }
  
  if (dir != 0 && dir != 1) dir = -1;

  // procfs #6 ... 
  // Strange, I could not found GPIO_MAX definition....
  // if (gpio >= GPIO_MAX) return -EINVAL;
  if (gpio < 0) {
    if(gpio == -1) { // assign read gpio #
      gpio_to_read = level;
    }
    else return -EINVAL;
  }
  else {
    //IMT8_printk("%s(%d, %d, %d, %d)\n", __func__, gpio, level, mode, dir);
    if (level == 1 || level == 0) procfs_set_gpio(gpio, level, mode, dir);
    else if (level == -1) {
      int gpio_value = 1 - procfs_get_gpio(gpio);
      procfs_set_gpio(gpio, gpio_value, mode, dir);
    }
    gpio_to_read = gpio;
  }

  return count;
}
#endif

#ifdef PROC_USBStatus
static struct proc_dir_entry *USBStatus_file;
static int proc_read_USBStatus(char *page, char **start, off_t off, int count, int *eof, void *data);
static int proc_write_USBStatus(struct file *file, const char *buffer, unsigned long count, void *data);
static int USBStatus_help = 0;
static int usb_currnet_status = 1;
static char USBStatus_HELP[] =
	"1. echo help > /proc/USBStatus\n"
	"2. echo USBON > /proc/USBStatus"
	"\t(USB Power on, will toggle usb mode, can use usb device)\n"
	"3. echo USBOFF > /proc/USBStatus"
	"\t(USB Power off, will toggle otg mode, can use otg usb port)\n"
	"4. echo KILLPID (my_pid) > /proc/USBStatus\n"
	"\t(can kill PID, example: echo KILLPID 1103 > /proc/USBStatus)\n"
	"5. echo CHMOD (premission) (device_note)+ > /proc/USBStatus\n"
	"\t(can chmod you path, example: echo CHMOD 0777 /dev/ttyUSB1+ > /proc/USBStatus)\n"
	"6. echo RMMOD (module_name)+ > /proc/USBStatus\n"
	"\t(can rmmod you module, example: echo RMMOD cp210x+ > /proc/USBStatus\n"
	"7. echo INSMOD (module_path)+ > /proc/USBStatus\n"
	"\t(can insmod you path, example: echo INSMOD /system/lib/modules/cp210x.ko+ > /proc/USBStatus\n";

static int proc_read_USBStatus(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char *p = page;
	int len = 0;

	if (USBStatus_help == 1) {
		USBStatus_help = 0;
		p += sprintf (p, USBStatus_HELP);
		IMT8_printk("[Po add] %s %s\n", __func__, p);
	}else if(usb_currnet_status == 1 ){
		char tmp[] = "USB Mode\n";
		p += sprintf (p, tmp);
		IMT8_printk("[Po add] %s %s\n", __func__, p);
	}else if(usb_currnet_status == 0 ){
		char tmp[] = "OTG Mode\n";
		p += sprintf (p, tmp);
		IMT8_printk("[Po add] %s %s\n", __func__, p);
	}

	*start = page + off;

	len = p - page;
	if (len <= off + count) *eof = 1;

	len -= off;
	if (len > count) len = count;
	if (len < 0) len = 0;
	return len;
}

//echo USBON > /proc/USBStatus
static int proc_write_USBStatus(struct file *file, const char *buffer, unsigned long count, void *data)
{
	char usb_data[50];

	if (count > 50){
		return -EINVAL;
	}else if (count == 0) {
		return 0;
	}

	if(copy_from_user(usb_data, buffer, count)){
		return -EFAULT;
	}

	if (usb_data[0] == 'h' || usb_data[0] == 'H') { // help
		USBStatus_help = 1;
		IMT8_printk("[Po add] %s Help\n", __func__);

	}else if (strncmp(usb_data, "USBOFF", 6) == 0) {

		usb_currnet_status = 0;
  		procfs_set_gpio(116, 1, -1, -1);
		IMT8_printk("[Po add] %s USBOFF\n", __func__);

	}else if (strncmp(usb_data, "USBON", 5) == 0) {

		usb_currnet_status = 1;
  		procfs_set_gpio(116, 0, -1, -1);
		IMT8_printk("[Po add] %s USBON\n", __func__);

	}else if (strncmp(usb_data, "KILLPID", 7) == 0){

		IMT8_printk("[Po add] %s usb_data= %s\n", __func__, usb_data);
		// strsep
		// KILLPID 123..
		char* const delim = " ";  
		char *token, *cur = usb_data;  
		char PID[50] = "";
		while (token = strsep(&cur, delim)) {  
			//IMT8_printk("[Po add] %s\n", token);  
			strcpy(PID, token);
		}
		IMT8_printk("[Po add] PID=%s\n", PID);  

		char * envp[] = { "HOME=/", NULL };
		char * argv[] = { "/system/bin/kill", PID, NULL};
		int ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC);
		if(ret == 0){
			IMT8_printk("[Po add] %s kill PID success\n", __func__);
		}else{
			IMT8_printk("[Po add] %s kill PID error, ret->%d\n", __func__, ret);
		}

	}else if (strncmp(usb_data, "CHMOD", 5) == 0){

		//IMT8_printk("[Po add] %s usb_data= %s\n", __func__, usb_data);
		// strsep
		// CHMOD 
		char* const delim = " ";  
		char *token, *cur = usb_data;  
		char premission[50] = "";
		char note[50] = "";
		int tmp_i = 0;
		while (token = strsep(&cur, delim)) {  
			tmp_i++;
			//IMT8_printk("[Po add] %s\n", token);  
			switch(tmp_i){
				case 2:
				strcpy(premission, token);
				case 3:
				strcpy(note, token);
			}
		}

		char* const delim2 = "+";  
		char *token2, *cur2 = note;  
		char device_note[50] = "";
		/*
		while (token2 = strsep(&cur2, delim2)) {  
			//IMT8_printk("[Po add] token2=%s\n", token2);  
			//IMT8_printk("[Po add] end\n");  

		}
		*/

		token2 = strsep(&cur2, delim2); 
		strcpy(device_note, token2);

		IMT8_printk("[Po add] premission=%s device_not=%s!\n", premission, device_note);  
		
		// chmod file
		char * envp[] = { "HOME=/", NULL };
		char * argv[] = { "/system/bin/chmod", premission, device_note, NULL};
		int ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC);
		if(ret == 0){
			IMT8_printk("[Po add] %s chmod %s %s success\n", __func__, premission, device_note);
		}else{
			IMT8_printk("[Po add] %s kill %s %s error, ret->%d\n", __func__, premission, note, ret);
		}
	
	}else if (strncmp(usb_data, "RMMOD", 5) == 0) {

		// strsep
		// RMMOD 
		char* const delim = " ";  
		char *token, *cur = usb_data;  
		char module_name[50] = "";
		int tmp_i = 0;
		while (token = strsep(&cur, delim)) {  
			tmp_i++;
			//IMT8_printk("[Po add] %s\n", token);  
			if(tmp_i == 2){
				strcpy(module_name, token);
			}
		}

		//IMT8_printk("[Po add] module_name=%s!\n", module_name);  
	
		char* const delim2 = "+";  
		char *token2, *cur2 = module_name;  
		char last_module_name[50] = "";
		/*
		while (token2 = strsep(&cur2, delim2)) {  
			//IMT8_printk("[Po add] token2=%s\n", token2);  
			//IMT8_printk("[Po add] end\n");  

		}
		*/
		token2 = strsep(&cur2, delim2); 
		strcpy(last_module_name, token2);

		// rmmod module
		char * envp[] = { "HOME=/", NULL };
		char * argv[] = { "/system/bin/rmmod", last_module_name, NULL};
		int ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC);
		if(ret == 0){
			IMT8_printk("[Po add] %s rmmod %s success\n", __func__, last_module_name);
		}else{
			IMT8_printk("[Po add] %s rmmod %s error, ret->%d\n", __func__, last_module_name, ret);
		}
	}else if (strncmp(usb_data, "INSMOD", 6) == 0) {

		// strsep
		// INSMOD 
		char* const delim = " ";  
		char *token, *cur = usb_data;  
		char module_name[100] = "";
		int tmp_i = 0;
		while (token = strsep(&cur, delim)) {  
			tmp_i++;
			//IMT8_printk("[Po add] %s\n", token);  
			if(tmp_i == 2){
				strcpy(module_name, token);
			}
		}

		//IMT8_printk("[Po add] module_name=%s!\n", module_name);  
	
		char* const delim2 = "+";  
		char *token2, *cur2 = module_name;  
		char last_module_name[100] = "";
		/*
		while (token2 = strsep(&cur2, delim2)) {  
			//IMT8_printk("[Po add] token2=%s\n", token2);  
			//IMT8_printk("[Po add] end\n");  

		}
		*/
		token2 = strsep(&cur2, delim2); 
		strcpy(last_module_name, token2);

		// rmmod module
		char * envp[] = { "HOME=/", NULL };
		char * argv[] = { "/system/bin/insmod", last_module_name, NULL};
		int ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC);
		if(ret == 0){
			IMT8_printk("[Po add] %s insmod %s success\n", __func__, last_module_name);
		}else{
			IMT8_printk("[Po add] %s insmod %s error, ret->%d\n", __func__, last_module_name, ret);
		}
	}



	return count;
}
#endif

#ifdef PROC_MTKIMEI
static struct proc_dir_entry *MTKIMEI_file;
static int proc_read_MTKIMEI(char *page, char **start, off_t off, int count, int *eof, void *data);
static int proc_write_MTKIMEI(struct file *file, const char *buffer, unsigned long count, void *data);
static int MTKIMEI_help = 0;
static int MTKIMEI_sim1 = 0;
static int MTKIMEI_sim2 = 0;
static char last_imei[100] = "";
static char MTKIMEI_HELP[] =
	"1. echo help > /proc/MTKIMEI\n"
	"2. echo SIM1 (IMEI code+) > /proc/MTKIMEI\n"
	"\t(can flash imei1, example: echo SIM1 555555555666666+ >  /proc/MTKIMEI\n"
	"3. echo SIM2 (IMEI code+) > /proc/MTKIMEI\n"
	"\t(can flash imei2, example: echo SIM2 777777777777777+ >  /proc/MTKIMEI\n";

void InitKernelEnv(void){ 
	oldfs = get_fs(); 
	set_fs(KERNEL_DS); 
} 

void DinitKernelEnv(){
	set_fs(oldfs); 
}

int ReadFile(struct file *fp,char *buf,int readlen) 
{ 
	if (fp->f_op && fp->f_op->read) 
		return fp->f_op->read(fp,buf,readlen, &fp->f_pos); 
	else 
		return -1; 
} 

int WriteFile(struct file *fp,char *buf,int readlen) { 
	if (fp->f_op && fp->f_op->read){ 
		//char buf2[50] = "AT+EGMR=1,7,\"123456789012347\"\n";
		//IMT8_printk("[Po add] %s 11 buf2=%s\n", __func__, buf2);  
		//IMT8_printk("[Po add] %s 11 buf=%s\n", __func__, buf);  
		return fp->f_op->write(fp,buf,readlen, &fp->f_pos); 
	}else{ 
		IMT8_printk("[Po add] %s 22\n", __func__);  
		return -1; 
	}
} 

struct file *OpenFile(char *path,int flag,int mode){ 
	struct file *fp; 

	fp=filp_open(path, flag, 0); 
	if (fp) return fp; 
	else return NULL; 
}

int CloseFile(struct file *fp) { 
	filp_close(fp,NULL); 
	return 0; 
}

static int proc_read_MTKIMEI(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char *p = page;
	int len = 0;

	if (MTKIMEI_help == 1) {
		MTKIMEI_help = 0;
		p += sprintf (p, MTKIMEI_HELP);
		IMT8_printk("[Po add] %s %s\n", __func__, p);
	}
#if 0
	}else if(MTKIMEI_sim1 == 1){
		IMT8_printk("[Po add] %s %s\n", __func__, last_imei);
		MTKIMEI_sim1 = 0;
		
		InitKernelEnv();
		struct file *fp; 

		//write to file
		fp = OpenFile("/dev/radio/pttycmd1", O_WRONLY | O_CREAT, 0644); 
		if (fp!= NULL) { 
			WriteFile(fp, last_imei, sizeof(last_imei));
			char tmp[] = "success\n";
			p += sprintf (p, tmp);
			IMT8_printk("[Po add] %s %s\n", __func__, p);
		}else{
			IMT8_printk("[Po add] fp = null\n");  
			char tmp[] = "error\n";
			p += sprintf (p, tmp);
			IMT8_printk("[Po add] %s %s\n", __func__, p);
		}

		CloseFile(fp); 
		DinitKernelEnv();


	}
#endif

	*start = page + off;

	len = p - page;
	if (len <= off + count) *eof = 1;

	len -= off;
	if (len > count) len = count;
	if (len < 0) len = 0;
	return len;
}

//echo valume > /proc/MTKIMEI
static int proc_write_MTKIMEI(struct file *file, const char *buffer, unsigned long count, void *data)
{
	char imei_data[50];

	if (count > 50){
		return -EINVAL;
	}else if (count == 0) {
		return 0;
	}

	if(copy_from_user(imei_data, buffer, count)){
		return -EFAULT;
	}

	if (imei_data[0] == 'h' || imei_data[0] == 'H') { // help
		MTKIMEI_help = 1;
		IMT8_printk("[Po add] %s Help\n", __func__);

	}else if (strncmp(imei_data, "SIM1", 4) == 0){

		IMT8_printk("[Po add] %s imei_data= %s\n", __func__, imei_data);

#if 0
		InitKernelEnv();
		// read
		//read file
		struct file *fp; 
		char read_buf[50] = "";
		char tmp_IMEICODE[20] = "";
		fp = OpenFile("/sdcard/sim1", O_RDONLY | O_CREAT, 0); 
		if(fp != NULL){ 
			ReadFile(fp, read_buf, sizeof(read_buf));
			IMT8_printk("[Po add] %s read_buf= %sx\n", __func__, read_buf);
			//IMT8_printk("[Po add] %s strlen(read_buf)= %d\n", __func__, strlen(read_buf));
			if(strlen(read_buf) > 15){
				char* const delim = "\0";  
				char *token, *cur = read_buf;  

				int len=(strlen(read_buf))-1, i; 
				for(i=0; i<len; i++) {
					tmp_IMEICODE[i]=read_buf[i]; 
				}
			}
		}else{
			IMT8_printk("[Po add] %s open file error!\n", __func__);
		}
		IMT8_printk("[Po add] %s tmp_IMEICODE=%s\n", __func__, tmp_IMEICODE);  

		CloseFile(fp); 
		//read End


		char cmd[70]="AT+EGMR=1,7,\"";
		strcat(cmd, tmp_IMEICODE);
		strcat(cmd, "\"\n");
		IMT8_printk("[Po add] cmd=%s\n", cmd);  


		//write to file
		fp = OpenFile("/dev/radio/pttycmd1", O_WRONLY | O_CREAT, 0644); 
		if (fp!= NULL) { 
			WriteFile(fp, cmd, sizeof(cmd));
		}else{
			IMT8_printk("[Po add] fp = null\n");  
		}


		CloseFile(fp);
		DinitKernelEnv();
		
		// read End
#endif
		// strsep
		// SIM1 123456789012347+
		char* const delim = " ";  
		char *token, *cur = imei_data;  
		char tmp_IMEICODE[50] = "";
		while (token = strsep(&cur, delim)) {  
			//IMT8_printk("[Po add] %s\n", token);  
			strcpy(tmp_IMEICODE, token);
		}
		// start 
		char* const delim2 = "+";  
		char *token2, *cur2 = tmp_IMEICODE;  
		char last_number[100] = "";
		/*
		while (token2 = strsep(&cur2, delim2)) {  
			//IMT8_printk("[Po add] token2=%s\n", token2);  
			//IMT8_printk("[Po add] end\n");  

		}
		*/
		token2 = strsep(&cur2, delim2); 
		strcpy(last_number, token2);
		IMT8_printk("[Po add] last_number=%s\n", last_number);  
		// End
		char tmp1[70]="AT+EGMR=1,7,\"";
		strcat(tmp1, tmp_IMEICODE);
		strcat(tmp1, "\"\n");
		IMT8_printk("[Po add] tmp1=%s\n", tmp1);  

		strcpy(last_imei, tmp1);
		IMT8_printk("[Po add] last_imei=%s\n", last_imei);  
		MTKIMEI_sim1=1;
		IMT8_printk("[Po add] MTKIMEI_sim1=%d\n", MTKIMEI_sim1);  

		struct file *fp; 

		//write to file
		InitKernelEnv();
		fp = OpenFile("/dev/radio/pttycmd1", O_WRONLY | O_CREAT, 0644); 
		if (fp!= NULL) { 
			WriteFile(fp, tmp1, sizeof(tmp1));
		}else{
			IMT8_printk("[Po add] fp = null\n");  
		}

		CloseFile(fp); 
		DinitKernelEnv();
	}else if (strncmp(imei_data, "SIM2", 4) == 0){

		IMT8_printk("[Po add] %s imei_data= %s\n", __func__, imei_data);
		// strsep
		// SIM2 777777777777777+
		char* const delim = " ";  
		char *token, *cur = imei_data;  
		char tmp_IMEICODE[50] = "";
		while (token = strsep(&cur, delim)) {  
			//IMT8_printk("[Po add] %s\n", token);  
			strcpy(tmp_IMEICODE, token);
		}
		// start 
		char* const delim2 = "+";  
		char *token2, *cur2 = tmp_IMEICODE;  
		char last_number[100] = "";
		/*
		while (token2 = strsep(&cur2, delim2)) {  
			//IMT8_printk("[Po add] token2=%s\n", token2);  
			//IMT8_printk("[Po add] end\n");  

		}
		*/
		token2 = strsep(&cur2, delim2); 
		strcpy(last_number, token2);
		IMT8_printk("[Po add] last_number=%s\n", last_number);  
		// End
		char tmp1[70]="AT+EGMR=1,10,\"";
		strcat(tmp1, tmp_IMEICODE);
		strcat(tmp1, "\"\n");
		IMT8_printk("[Po add] tmp1=%s\n", tmp1);  

		strcpy(last_imei, tmp1);
		IMT8_printk("[Po add] last_imei=%s\n", last_imei);  
		MTKIMEI_sim2=1;
		IMT8_printk("[Po add] MTKIMEI_sim2=%d\n", MTKIMEI_sim2);  

		struct file *fp; 

		//write to file
		InitKernelEnv();
		fp = OpenFile("/dev/radio/pttycmd1", O_WRONLY | O_CREAT, 0644); 
		if (fp!= NULL) { 
			WriteFile(fp, tmp1, sizeof(tmp1));
		}else{
			IMT8_printk("[Po add] fp = null\n");  
		}

		CloseFile(fp); 
		DinitKernelEnv();
	}
	return count;
}

#endif

static int __init proc_imobile_procfs_init(void)
{
#ifdef PROC_GPIO
	// for /proc/gpio
	gpio_file = create_proc_entry ("gpio", 0666, NULL);
	gpio_file->read_proc = proc_read_gpio; // ref #4
	gpio_file->write_proc = proc_write_gpio; // ref #5
#endif

#ifdef PROC_KBD
	// for /proc/kbd
	kpd_file = create_proc_entry ("kpd", 0666, NULL);
	kpd_file->read_proc = proc_read_kpd; // ref #4
	kpd_file->write_proc = proc_write_kpd; // ref #5
#endif

#ifdef PROC_ETH0
	// for /proc/eth0
	eth0_file = create_proc_entry ("eth0", 0666, NULL);
	eth0_file->read_proc = proc_read_eth0;
	eth0_file->write_proc = proc_write_eth0;
#endif

#ifdef PROC_USBStatus
	// for /proc/USBStatus
	USBStatus_file = create_proc_entry ("USBStatus", 0666, NULL);
	USBStatus_file->read_proc = proc_read_USBStatus;
	USBStatus_file->write_proc = proc_write_USBStatus;
#endif

#ifdef PROC_MTKIMEI
	// for /proc/MTKIMEI
	MTKIMEI_file = create_proc_entry ("MTKIMEI", 0666, NULL);
	MTKIMEI_file->read_proc = proc_read_MTKIMEI;
	MTKIMEI_file->write_proc = proc_write_MTKIMEI;
#endif
	//Enable USB power 
	procfs_set_gpio(116, 0, -1, -1);
	return 0;
}

module_init(proc_imobile_procfs_init);
