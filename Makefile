# 获取当前目录下的所有c文件
SRC = $(wildcard *.c)
# 将src中的所有.c文件替换为.o文件
OBJS = $(patsubst %.c,%.o,$(SRC))
# 编译器
CC = gcc
# flag
FLAGS =
# 头文件包含路径
INCLUDE = -I.
# 库文件
LIBS = -lpthread
# 库文件
LIBS_PATH =
# 目标执行文件
TARGET = test

$(TARGET): $(OBJS)
	$(CC) $(FLAGS) -o $(TARGET) $(OBJS) $(LIBS_PATH) $(LIBS)
	rm -f $(OBJS)

$(OBJS): $(SRC)	
	$(CC) $(FLAGS) -c $(SRC) $(INCLUDE) $(LIBS_PATH) $(LIBS)

.PHONY: clean
clean:
	rm -f *.o $(TARGET)


