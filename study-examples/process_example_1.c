int main() {
	pid_t n;
	n = fork(); /* the fork creates a child process exactly
			  * the same to its parent and returns 
			  * child's pid to the parent
			  * After fork() is executed: 
			  * child process with pid=y holds n=0 inside
			  * parent process with pid=x holds n=y inside
			  */
	if (n<0) { // error occurred
		fprintf(stderr, "Fork failed");
		exit(-1);
	}
	else if(n==0){ // child process will execute this
			    // since n=0
		execlp("/bin/ls","ls",NULL); 
		// child's address space was the same as its parent
 		// before execlp. Execlp wipes the child's address
		// space and loads ls command
	}
	else { // n>0 holds only when n=y, and parent process
		  // holds n=y, so parent process will execute
		wait(NULL); //enforced to wait until child 
				 // terminates		
		printf("Child complete");
		exit(0); //terminates parent process
	}
}