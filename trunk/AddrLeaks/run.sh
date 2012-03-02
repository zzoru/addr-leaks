opt -load ../../../Release/lib/AddrLeaks.so -mem2reg -instnamer -internalize -inline -globaldce -addrleaks < "$1" #> /dev/null &&
dot -Tpng module.dot -o module.png &&
eog module.png
