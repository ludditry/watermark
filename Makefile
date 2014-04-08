TARGET=watermark

$(TARGET): main.o
	$(CC) $< -o $@ -ljpeg

clean:
	rm -rf *.o $(TARGET)

