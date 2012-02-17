opt -load ../../../Release/lib/AddrLeaks.so -instnamer -internalize -inline -addrleaks < "tests/$1.bc" > /dev/null &&
dot -Tpng module.dot -o module.png &&
eog module.png
