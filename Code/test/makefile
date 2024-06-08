src=$(wildcard *.cpp)
obj=$(patsubst %.cpp,%.o,$(src))

myArgs= -Wall -g -pthread

All:test#指定最终目标，否则默认第一个


$(obj):%.o:%.cpp 
	g++ -c $< -o $@ $(myArgs)
test:$(obj)
	g++ $^ -o $@ $(myArgs)

clean:
	-rm -rf $(obj) test 
	
.PHONY: clean ALL 
