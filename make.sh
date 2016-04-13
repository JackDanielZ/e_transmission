cd src
gcc -fPIC -c e_mod_main.c $CFLAGS `pkg-config --cflags enlightenment elementary` -o e_mod_main.o
gcc -shared -fPIC -DPIC e_mod_main.o `pkg-config --libs enlightenment elementary` -Wl,-soname -Wl,module.so -o module.so
cd -

/opt/e/bin/edje_cc -v -id ./images e-module-cpu.edc e-module-cpu.edj
/opt/e/bin/edje_cc -v -id ./images cpu.edc cpu.edj

release=$(pkg-config --variable=release enlightenment)
host_cpu=$(uname -m)
MODULE_ARCH="linux-gnu-$host_cpu-$release"

sudo /usr/bin/mkdir -p '/opt/e/lib/enlightenment/modules/cpu/$MODULE_ARCH'
sudo /usr/bin/install -c src/module.so /opt/e/lib/enlightenment/modules/cpu/$MODULE_ARCH/module.so
sudo /usr/bin/install -c -m 644 e-module-cpu.edj cpu.edj '/opt/e/lib/enlightenment/modules/cpu'
