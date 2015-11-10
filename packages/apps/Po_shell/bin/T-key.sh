#date >> /T-key.sh.log
read_status=`gpio-tools.sh r 75`

echo $read_status
if [ "$read_status" == "1 0 1" ]; then
echo "1"
swirch=`gpio-tools.sh e 75`
#$switch_barcode
else
	if [ "$read_status" == "1 0 0" ]; then
	echo "2 1"
	echo "75 1 0 1" > /proc/gpio
	switch=`gpio-tools.sh e 75`
	else
	echo "2 2"
	switch=`gpio-tools.sh e 75`
	fi
fi

#echo "182 0 0 1" > /proc/gpio

