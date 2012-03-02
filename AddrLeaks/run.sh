opt -load ../../../Release/lib/AddrLeaks.so -instnamer -internalize -inline -globaldce -addrleaks < "$1" #> /dev/null &&
dot -Tpng module.dot -o module.png &&
eog module.png
