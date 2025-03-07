
../bin/x86/linux/threads: threads.cpp
	g++ -o ../bin/x86/linux/threads threads.cpp -pthread -std=c++11

../bin/arm/linux/threads: threads.cpp
	aarch64-linux-gnu-g++ -static -o ../bin/arm/linux/threads threads.cpp -pthread -std=c++11

../bin/riscv/linux/threads: threads.cpp
	riscv64-linux-gnu-g++ -static -o ../bin/riscv/linux/threads threads.cpp -pthread -std=c++11
