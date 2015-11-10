PATH=/system/busybox:/sbin:/vendor/bin:/system/sbin:/system/bin:/system/xbin

check_Wifi() {
  netcfg | grep wlan0 && Po_Wifi_status=1 || Po_Wifi_status=0 
}

disable_wifi(){
  echo "disable wifi"
  svc wifi disable
}

enable_wifi(){
  echo "enable wifi"
  svc wifi enable
}

Po_Wifi_status=0
check_Wifi
echo "Po_Wifi_status=$Po_Wifi_status"


if [ $Po_Wifi_status -eq "1" ]; then
  echo "have wifi"  
  disable_wifi
else
  echo "not wifi"
  enable_wifi
fi
