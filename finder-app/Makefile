# Определяем компилятор. По умолчанию используется gcc (нативная сборка)
CC = $(CROSS_COMPILE)gcc

# Цель по умолчанию: собирает приложение "writer"
all: writer

# Правило сборки исполняемого файла из объектного файла
writer: writer.o
	$(CC) writer.o -o writer

# Правило сборки объектного файла из исходного кода .c
writer.o: writer.c
	$(CC) -c writer.c -o writer.o

# Цель очистки: удаляет исполняемый файл и все файлы .o
clean:
	rm -f writer writer.o