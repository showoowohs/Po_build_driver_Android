dmesg > dmsg_from_bin.log
export PATH=/system/busybox:$PATH
mount -o remount,rw /dev/block/mtdblock3 /
mount -o remount,rw /emmc@android /system

if [ ! -e /system/busybox ]; then
cd /system
mkdir busybox
cd busybox
/system/bin/busybox --install .
fi

#while [ ! -f /dev/.coldboot_done ]; do     
#  sleep 1;                                 
#done
sleep 10;

for script in /system/local_script/*.sh; do
  /system/busybox/ash $script &
done

#export PATH=/system/busybox:$PATH

