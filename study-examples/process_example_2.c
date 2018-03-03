//Warning: pseudo C code
int main() {
	pid_t n = 0;

	/* What happens in parent process:
	 * At each iteration of the first loop:
	 * parent process creates a new child process. (a total 
	 * of 10 children will be created). 
	 * then checks if n==0 (n will be replaced with child's
	 * process id by then, so will return false). finishes 
	 * the iteration.
	 * At each iteration of the second loop:
	 * parent process waits for a child to terminate (it will 
	 * wait for all of its 10 children).
	 *
	 * What happens in children processes: 
	 * Each one continues executing after the fork statement 	 *(i.e. starts with if statement). Child process prints
	 * hello and exits.
	 */ 
	for(int i = 0; i < 10; i++) {
		n = fork();
		if(n==0){
			print("hello");
			exit(0);
		}
	}

	for(int i = 0; i <10; i++)
		wait();
}