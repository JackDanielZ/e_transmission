gcc -fPIC -c src/e_mod_main.c $CFLAGS `pkg-config --cflags enlightenment elementary` -o src/e_mod_main.o
[ $? -eq 0 ] || exit 1
gcc -shared -fPIC -DPIC src/e_mod_main.o `pkg-config --libs enlightenment elementary` -Wl,-soname -Wl,module.so -o src/module.so
[ $? -eq 0 ] || exit 1

/opt/e/bin/edje_cc -v -id ./images e-module-transmission.edc e-module-transmission.edj
[ $? -eq 0 ] || exit 1
/opt/e/bin/edje_cc -v -id ./images transmission.edc transmission.edj
[ $? -eq 0 ] || exit 1

release=$(pkg-config --variable=release enlightenment)
host_cpu=$(uname -m)
MODULE_ARCH="linux-gnu-$host_cpu-$release"

sudo /usr/bin/mkdir -p '/opt/e/lib/enlightenment/modules/transmission/'$MODULE_ARCH
sudo /usr/bin/install -c src/module.so /opt/e/lib/enlightenment/modules/transmission/$MODULE_ARCH/module.so
sudo /usr/bin/install -c module.desktop /opt/e/lib/enlightenment/modules/transmission/module.desktop
sudo /usr/bin/install -c -m 644 e-module-transmission.edj transmission.edj '/opt/e/lib/enlightenment/modules/transmission'
