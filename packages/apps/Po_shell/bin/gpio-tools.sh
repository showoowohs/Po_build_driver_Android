#!/system/bin/sh
PATH=/system/busybox:$PATH
VERBOSE=0
[ "x$1" = "x-v" ] && VERBOSE=1 && shift
CMD=$1
GPIO=$2
VALUE=$3
MODE=$4
DIR=$5

help()
{
echo "USAGE: $0 CMD [-v] [GPIO [VALUE]]"
echo "  CMD : help, (r)ead, (w)rite, (d)ir, (m)ode, (e)xchange, (s)et"
echo "    -v for verbose"
echo "EXAMPLE:"
echo "  $0 help        # get this help message"
echo "  $0 r 186       # read gpio #186"
echo "  $0 w 186 1     # set gpio #186 into 1"
echo "  $0 d 186 1     # set gpio #186 dir into 1(OUT), 0 for (IN)"
echo "  $0 D 186       # exchange gpio #186 dir between 1(OUT), 0 (IN)"
echo "  $0 m 186 0     # set gpio #186 mode into 0(GPIO), 1(INT), 7(MAX)"
echo "  $0 e 186       # exchange gpio #186 level between 0 and 1"
echo "  $0 s 186 1 0 1 # set gpio #186 to (Level,Mode,Dir) in (1,0,1)"
exit 1
}

[ "x$CMD" = "x" -o "x$CMD" = "xhelp" ] && help

# 1. write -1 to select gpio#, usage as:
#	echo "-1 GPIO#" > /proc/gpio
#    for example, select GPIO67 to be read:
#       echo "-1 67" > /proc/gpio
# 2. read from /proc/gpio again, such as:
#	cat /proc/gpio
read_gpio()
{
  G=$1
  V=$2
  GV=$( ( echo -1 $G > /proc/gpio && cat /proc/gpio ) |
    sed 's/.*(\([0-9]\),\([0-9]\),\([0-9]\))/\1 \2 \3/g' )
  if [ "x$V" = "x1" ]; then
    echo $GV | while read Level Mode Dir; do
      echo "Level=$Level\nMode=$Mode\nDir=$Dir"
    done
  else
    echo "$GV"
  fi
}

[ "x$CMD" = "xr" -o "x$CMD" = "xread" ] && read_gpio $GPIO $VERBOSE && exit 0

# 3. write usage normally is:
#	echo "GPIO# LEVEL [GPIO_MODE [GPIO_DIR]]" > /proc/gpio
#    where LEVEL could be [0,1,-1], -1 to switch 1 from 0 or 0 from 1
#    where mode could be 0..7, and dir could be 0(IN) or 1(OUT)
write_value()
{
  G=$1
  V=$2
  echo $G $V > /proc/gpio
  [ "x$VERBOSE" = "x1" ] && read_gpio $G $VERBOSE
}

[ "x$CMD" = "xw" -o "x$CMD" = "xwrite" ] && write_value $GPIO $VALUE && exit 0

change_dir()
{
  G=$1
  read_gpio $G 0 | while read Level Mode Dir; do
    if [ "x$Dir" = "x1" ]; then
      echo $G $Level $Mode 0 > /proc/gpio
    else
        echo $G $Level $Mode 1 > /proc/gpio
    fi
  done
  [ "x$VERBOSE" = "x1" ] && read_gpio $G 1
}

[ "x$CMD" = "xD" -o "x$CMD" = "xDir" ] && change_dir $GPIO $VALUE && exit 0

write_dir()
{
  G=$1
  D=$2
  read_gpio $G 0 | while read Level Mode Dir; do
    echo $G $Level $Mode $D > /proc/gpio
  done
  [ "x$VERBOSE" = "x1" ] && read_gpio $G 1
}

[ "x$CMD" = "xd" -o "x$CMD" = "xdir" ] && write_dir $GPIO $VALUE && exit 0

write_mode()
{
  G=$1
  M=$2
  read_gpio $G 0 | while read Level Mode Dir; do
    echo $G $Level $M $Dir > /proc/gpio
  done
  [ "x$VERBOSE" = "x1" ] && read_gpio $G 1
}

[ "x$CMD" = "xd" -o "x$CMD" = "xdir" ] && write_dir $GPIO $VALUE && exit 0

exchange_level()
{
  G=$1
  read_gpio $G 0
  echo $G -1 > /proc/gpio
  [ "x$VERBOSE" = "x1" ] && read_gpio $G 1
}

[ "x$CMD" = "xe" -o "x$CMD" = "xexchange" ] && 
  exchange_level $GPIO && exit 0

set_gpio()
{
  G=$1
  L=$2
  M=$3
  D=$4
  echo $G $L $M $D > /proc/gpio
  [ "x$VERBOSE" = "x1" ] && read_gpio $G 1
}

[ "x$CMD" = "xs" -o "x$CMD" = "xset" ] && 
  set_gpio $GPIO $VALUE $MODE $DIR && exit 0
