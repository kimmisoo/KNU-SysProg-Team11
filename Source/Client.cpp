#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> 
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <signal.h>
#include <termios.h>

#define BUF_SIZE 100
#define NAME_SIZE 20
#define CMD_SIZE 15

// 프로토타입.
void * recv_msg(void * arg);
void error_handling(char * msg);
void set_name(char* name, int sock);
void make_room(int sock);
void menu(int sock);
void enter_room(int sock);
void room_msg(int sock);
void keycontrol(int sig);
void set_cr_noecho_mode(void);
void tty_mode(int);

// 전역변수
char name[NAME_SIZE] = "[DEFAULT]";
char msg[BUF_SIZE];
int rcv_trigger;
int check_position=0; 
int sock;
//클라이언트의 상태를 나타내기위한 변수. ex)채팅방에 접속중이다.

int main(int argc, char *argv[])
{
	struct sockaddr_in serv_addr;
	pthread_t snd_thread, rcv_thread;
	void * thread_return;
	char port[10] = "";
	char address[20] = "";
	char nameInput[20] = "";
	struct sigaction act;
	act.sa_handler = keycontrol;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;

	tty_mode(0);
	//CTL+C 입력시 시그널 호출.
	sigaction(SIGINT, &act, 0);

	// stdin으로 입력을 받아서 port, ip, 대화명을 저장.
	// TODO: 입력 예외 처리
	printf("Port : ");
	scanf("%s", port);
	printf("Server IP : ");
	scanf("%s", address);
	printf("Your nickname : ");
	scanf("%s", name);

	sock = socket(PF_INET, SOCK_STREAM, 0);

	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr(address);
	serv_addr.sin_port = htons(atoi(port));

	if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
		error_handling("connect() error");

	printf("Successfully Connected\n");
	printf("----------------------\n");
	check_position = 1;

	pthread_create(&rcv_thread, NULL, recv_msg, (void*)&sock);

	set_name(name, sock);
	while (getchar() != '\n');
	menu(sock);

	pthread_join(rcv_thread, &thread_return);
	

	close(sock);
	return 0;
}

/*
메뉴 함수. stdin으로 사용자에게 수행할 명령을 받아서 해당 함수를 호출한다.
*/
void menu(int sock)
{
	char command = '0';
	char kbd_msg[BUF_SIZE];
	int kInput;
	int end = 0;

	while (1)
	{
		printf("1)Make a new chatroom. 2)Connect to existing chatroom. q)Disconnect.\n");
		set_cr_noecho_mode();
		//gets(kbd_msg);
		kInput = getchar();
		tty_mode(1);


			switch (/*kbd_msg[0]*/kInput)
			{
			case '1':
				// 새로운 방 생성.
				make_room(sock);
				break;
			case '2':
				// 방 입장.
				enter_room(sock);
				break;
			case 'q':
				// 나가기.
				close(sock);
				exit(0);
				break;
			default:
				// 이외엔 입력 제대로 하란 메시지 출력
				//write(sock, kbd_msg, strlen(kbd_msg));
				printf("Invalid Input\n");	
				break;
		}
	}
		// 이외엔 입력 제대로 하란 메시지 출력
}

/*
이름 설정용 함수. 서버로 #setname@@이름 을 전송함.
입력 : 사용할 이름(char*), 소켓번호(int)
*/
void set_name(char* name, int sock)
{
	char naming_msg[NAME_SIZE + CMD_SIZE];
	sprintf(naming_msg, "#setname@@%s", name);
	write(sock, naming_msg, strlen(naming_msg));
}

/*
새 방 생성 함수. 서버로 #makeroom@@이름 을 전송함.
입력 : 소켓번호(int)
*/
void make_room(int sock)
{
	char room_name[NAME_SIZE];
	char room_pass[NAME_SIZE];
	char room_option;
	char temp_str[NAME_SIZE + CMD_SIZE];

	check_position++;
	printf("New room's name    : ");
	scanf("%s", room_name);
	getchar();
	printf("Set_password (Y/N) : ");
	
	while(1)
	{
		set_cr_noecho_mode();
		room_option = getchar();
		tty_mode(1);
		if (room_option == 'Y' || room_option == 'y')
		{
			printf("Password           : ");
			scanf("%s", room_pass);
			sprintf(temp_str, "#makeroom@@%c$$%s&&%s",room_option, room_name, room_pass);
			break;
		}
		else if (room_option == 'N' || room_option == 'n')
		{
			sprintf(temp_str, "#makeroom@@%c$$%s",room_option, room_name);
			break;
		}
		else
			printf("Invalid command. press Y or N\n");
	}
	write(sock, temp_str, strlen(temp_str));

	while (getchar() != '\n');
	while (1)
	{
		if (rcv_trigger == 1)
		{
			rcv_trigger = 0;
			room_msg(sock);
			break;
		}
		else if (rcv_trigger == 2)
		{
			rcv_trigger = 0;
			break;
		}
	}
}

/*
방 입장 함수. 서버로 #enterroom@@이름 을 전송함.
입력 : 소켓번호(int)
*/
void enter_room(int sock)
{
	char room_name[NAME_SIZE];
	char temp_str[NAME_SIZE + CMD_SIZE];
	
	printf("room's name : ");
	scanf("%s", room_name);
	sprintf(temp_str, "#enterroom@@%s", room_name);
	write(sock, temp_str, strlen(temp_str));


	while (getchar() != '\n');
	while (1)
	{
		if (rcv_trigger == 1)
		{
			rcv_trigger = 0;
			check_position++;
			room_msg(sock);
			break;
		}
		else if (rcv_trigger == 2)
		{
			rcv_trigger = 0;
			break;
		}
	}
}

/*
방 내부채팅 함수. 서버로 #chatroom@@메시지 를 전송함.
입력 : 소켓번호(int)
*/
void room_msg(int sock)
{
	char full_msg[BUF_SIZE + CMD_SIZE];
	memset(full_msg, 0, sizeof(full_msg));

	while (1)
	{
		fgets(msg, BUF_SIZE, stdin);
		if (!strcmp(msg, "q\n") || !strcmp(msg, "Q\n")) //////////////////////
		{
			sprintf(full_msg, "#chatroom@@%s", msg);
			write(sock, full_msg, strlen(full_msg));
			break;
		}
		sprintf(full_msg, "#chatroom@@%s", msg);
		write(sock, full_msg, strlen(full_msg));
	}

	check_position--;
	rcv_trigger = 0;
	return;
}

/*
메시지 수신 쓰레드.
메시지 수신은 여기서 다 처리함.
*/
void * recv_msg(void * arg)   // read thread main
{
	int sock = *((int*)arg);
	char name_msg[NAME_SIZE + BUF_SIZE + 5];
	char password[300];
	int str_len;
	while (1)
	{
		str_len = read(sock, name_msg, NAME_SIZE + BUF_SIZE + 5 - 1);
		if (str_len == -1)
			return (void*)-1;
		name_msg[str_len] = 0;
		fputs(name_msg, stdout);

		// 채팅방 입장했는지 못했는지 서버에서 받아서 처리.
		// rcv_trigger는 서버에서 결과를 받기 전까지 sleep 시키기 위해 만든 변수.
		
		if (strcmp(name_msg, "Connecting to existing room.\n") == 0
			|| strcmp(name_msg, "New room is created.\n") == 0)
		{
			rcv_trigger = 1;
		}
		else if(strcmp(name_msg, "Can't find a room.\n") == 0
			|| strcmp(name_msg, "Password Error\n") ==0 )
		{
			rcv_trigger = 2;
		}
		else if (strcmp(name_msg, "Please Password input : ") == 0)
		{
			scanf("%s", password);
			write(sock, password, strlen(password));
			getchar();
		}
			
	}
	return NULL;
}

//CTL+C 입력시 시그널 함수.
void keycontrol(int sig)
{
	char full_msg[BUF_SIZE + CMD_SIZE];
	memset(full_msg, 0, sizeof(full_msg));

	if (sig == SIGINT)
		printf("Client forced termination\n");
	if (check_position == 0)  
		exit(1);
	else if (check_position == 1) //소켓이 만들어졌을때.
		close(sock);
	else  //채팅방에 접속중일 때.
	{
		strcpy(msg, "Q\n");
		sprintf(full_msg, "#chatroom@@%s", msg);
		write(sock, full_msg, strlen(full_msg));
		close(sock);
	}
	tty_mode(1);
	exit(1);
}
void error_handling(char *msg)
{
	fputs(msg, stderr);
	fputc('\n', stderr);
	exit(1);
}

void tty_mode(int how)
{
	static struct termios original_mode;
	if(how == 0)
		tcgetattr(0, &original_mode);
	else
		tcsetattr(0, TCSANOW, &original_mode);
}

void set_cr_noecho_mode()
{
	struct termios ttystate;

	tcgetattr(0, &ttystate);
	ttystate.c_lflag &= ~ICANON;
	ttystate.c_lflag &= ~ECHO;
	ttystate.c_cc[VMIN] = 1;
	tcsetattr(0, TCSANOW, &ttystate);
}
