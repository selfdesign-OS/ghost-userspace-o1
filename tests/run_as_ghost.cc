#include <iostream>
#include <vector>
#include <cstdlib>  // For execvp
#include <unistd.h> // For fork, execvp, etc.
#include <sys/wait.h> // For waitpid
#include "lib/base.h"  // Include ghOSt related headers
#include "lib/ghost.h" // Include ghOSt related headers
#include "lib/topology.h"


  

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <program-to-run> [args...]" << std::endl;
        return 1;
    }

    // Extract the program and its arguments from argv
    std::vector<char*> program_args(argv + 1, argv + argc);
    program_args.push_back(nullptr); // Null-terminate the argument list

    // Print the program and arguments for debugging purposes
    std::cout << "Running program: " << argv[1] << std::endl;

    // Fork the process to run the program in a child process
    pid_t pid = fork();
    if (pid == 0) {
        // Child process: ghOSt 마이그레이션 후 프로그램 실행

        // 1. ghOSt 스케줄러로 프로세스 마이그레이션
        // CPU affinity를 설정하여 CPU 1에 고정
        //ghost::GhostHelper()->SchedSetAffinity(ghost::Gtid::Current(),
        //                                ghost::MachineTopology()->ToCpuList(std::vector<int>{1,2,3}));

        // 2. ghOSt 스케줄링 클래스로 마이그레이션
        if (ghost::GhostHelper()->SchedTaskEnterGhost(/*pid=*/0, /*dir_fd=*/-1) != 0) {
            std::cerr << "Failed to enter ghOSt scheduling class!" << std::endl;
            return 1;
        }

        // 3. 프로그램 실행
        execvp(program_args[0], program_args.data());
        // execvp가 실패하면 에러 메시지를 출력
        perror("execvp failed");
        return 1;
    } else if (pid > 0) {
        // Parent process: 자식 프로세스가 끝날 때까지 기다림
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            std::cout << "Program exited with status " << WEXITSTATUS(status) << std::endl;
        } else {
            std::cerr << "Program did not terminate correctly." << std::endl;
        }
    } else {
        // Fork 실패
        std::cerr << "Fork failed!" << std::endl;
        return 1;
    }

    return 0;
}
