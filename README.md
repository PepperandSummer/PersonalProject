# myShell_nyuOSlab2
## A simplified version of the Unix shell. Please click this link to see details.
## https://cs.nyu.edu/courses/spring23/CSCI-GA.2250-002/nyush

Milestone 1. Write a simple program that prints the prompt and flushes STDOUT. You may use the getcwd() system call to get the current working directory.

Milestone 2. Write a loop that repeatedly prints the prompt and gets the user input. You may use the getline() library function to obtain user input.

Milestone 3. Extend Milestone 2 to be able to run a simple program, such as ls. At this point, what you’ve written is already a shell! Conceptually, it might look like:

Milestone 4. Extend Milestone 3 to run a program with arguments, such as ls -l.

Milestone 5. Handle simple built-in commands (cd and exit). You may use the chdir() system call to change the working directory.

Milestone 6. Handle output redirection, such as cat > output.txt.

Milestone 7. Handle input redirection, such as cat < input.txt.

Milestone 8. Run two programs with one pipe, such as cat | cat. The example code in man 2 pipe is very helpful.

Milestone 9. Handle multiple pipes, such as cat | cat | cat.

Milestone 10. Handle suspended jobs and related built-in commands (jobs and fg). Read man waitpid to see how to wait for a stopped child.

Some students may find handling suspended jobs easier than implementing pipes, so feel free to rearrange these milestones as you see fit.
