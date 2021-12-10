OUT_FILE := OS
all: install
install: 
	@make clean
	gcc main.c -o ${OUT_FILE} -l pthread > /dev/null 2>&1
clean:
	rm ${OUT_FILE} || true
	rm *.o || true
