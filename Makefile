CC = gcc

mysh: mysh.c
	$(CC)  $^ -o $@

%.o: %.c
	$(CC) -c $< -o $@
