.PHONY: all blade computer test clean

all: test blade computer

blade:
	pio run -d firmware/blade -e blade

computer:
	pio run -d firmware/computer -e computer

test:
	cmake -S tests/host -B .build/host
	cmake --build .build/host
	ctest --test-dir .build/host --output-on-failure

clean:
	pio run -d firmware/blade -e blade -t clean
	pio run -d firmware/computer -e computer -t clean
	cmake --build .build/host --target clean
