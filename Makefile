main.o: main.c Listen.h
	gcc -c main.c

Listen.o: Listen.h
	gcc -c Listen.h

webserver: main.o Listen.h
	gcc main.c Listen.h -o webserver

run: webserver
	./webserver

clean:
	rm -f *.o app