#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define BUF_SIZE 100
#define MAX_CLNT 256
#define NAME_SIZE 20

// 프로토 타입
void * handle_clnt(void * arg);
void send_msg(char * msg, int len);
void error_handling(char * msg);
void * server_ctrl(void * arg);
void handle_msg(char* msg, int sock);
void set_name(char* name, int sock);
void make_room(char* room_data, int sock);
void enter_room(char* room_name, int sock);
void return_msg(char * msg, int sock);
void chat_room(char* room_name, int sock);
void connect_msg(int sock_index);
void keycontrol(int sig);

typedef struct option
{
	int person_num;
	int set_secret; // 1이면 secret방 0이면 일반방.
	char pass_word[300];
}option;

// 전역 변수
int clnt_cnt = 0;
int clnt_socks[MAX_CLNT];
char clnt_name[MAX_CLNT][NAME_SIZE];
int clnt_room[MAX_CLNT];
int room_cnt = 0;
option room_set[MAX_CLNT];
char rooms[MAX_CLNT][NAME_SIZE];
pthread_mutex_t mutx;
int serv_sock_save;

int main(int argc, char *argv[])
{
	int serv_sock, clnt_sock;
	struct sockaddr_in serv_adr, clnt_adr;
	int clnt_adr_sz, i;
	pthread_t t_id;
	char port[10] = "";
	struct sigaction act;
	act.sa_handler = keycontrol;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	
	//CTL+C 입력시 시그널 호출.
	sigaction(SIGINT, &act, 0);

	for (int i = 0; i < MAX_CLNT; i++)
	{
		clnt_room[i] = -1;
	}

	// stdin으로 port 받기
	printf("Port : ");
	scanf("%s", port);

	pthread_mutex_init(&mutx, NULL);
	serv_sock = socket(PF_INET, SOCK_STREAM, 0);

	memset(&serv_adr, 0, sizeof(serv_adr));
	serv_adr.sin_family = AF_INET;
	serv_adr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_adr.sin_port = htons(atoi(port));

	if (bind(serv_sock, (struct sockaddr*) &serv_adr, sizeof(serv_adr)) == -1)
		error_handling("bind() error");
	if (listen(serv_sock, 5) == -1)
		error_handling("listen() error");
	serv_sock_save = serv_sock;

	// 서버 관리 쓰레드 시작.
	pthread_create(&t_id, NULL, server_ctrl, (void*)&server_ctrl);

	while (1)
	{
		clnt_adr_sz = sizeof(clnt_adr);
		clnt_sock = accept(serv_sock, (struct sockaddr*)&clnt_adr, (socklen_t*)&clnt_adr_sz);

		pthread_mutex_lock(&mutx);
		clnt_socks[clnt_cnt++] = clnt_sock;
		pthread_mutex_unlock(&mutx);

		pthread_create(&t_id, NULL, handle_clnt, (void*)&clnt_sock);
		pthread_detach(t_id);
		printf("Connected client IP: %s \n", inet_ntoa(clnt_adr.sin_addr));
	}
	close(serv_sock);
	return 0;
}

// 새 클라이언트가 접속하면 이 쓰레드로 분리됨.
// 소스코드 복사해서 약간 수정함.
void * handle_clnt(void * arg)
{
	int clnt_sock = *((int*)arg);
	int str_len = 0, i;
	char msg[BUF_SIZE];
	memset(msg, 0, sizeof(msg));

	while ((str_len = read(clnt_sock, msg, sizeof(msg))) != 0)
	{
		handle_msg(msg, clnt_sock);
		memset(msg, 0, sizeof(msg));
	}

	pthread_mutex_lock(&mutx);
	for (i = 0; i < clnt_cnt; i++)   // remove disconnected client
	{
		if (clnt_sock == clnt_socks[i])
		{
			printf("%s is disconnected.\n", clnt_name[i]);

			//TODO: 채팅방 관련된 정보도 삭제해야 함.
			while (i++ < clnt_cnt - 1)
			{
				clnt_socks[i] = clnt_socks[i + 1];
				strcpy(clnt_name[i], clnt_name[i + 1]);
			}
			break;
		}
	}

	clnt_cnt--;
	pthread_mutex_unlock(&mutx);
	close(clnt_sock);
	return NULL;
}

/*
서버 관리 쓰레드. stdin으로 q를 입력하면 프로그램이 종료된다.
*/
void * server_ctrl(void * arg)
{
	int sock = *((int*)arg);
	char msg[50];

	while (1)
	{
		fgets(msg, BUF_SIZE, stdin);
		if (!strcmp(msg, "q\n") || !strcmp(msg, "Q\n"))
		{
			close(sock);
			exit(0);
		}
	}
	return NULL;
}

/*
메시지 관리 함수. 클라이언트에게서 받은 메시지를 분석해 필요한 함수를 호출.
명령어와 내용은 @@로 구분됨. (ex: #setname@@kim = 이름설정 -> kim으로)
#setname : 이름 설정, 저장.
입력 : 메세지(char*), 소켓 파일 디스크립터(int)
*/
void handle_msg(char* msg, int sock)
{
	char sep[3] = "@@";
	char* str = strtok(msg, sep);
	int i;
	
	// 이름 설정 메시지 수신 시.
	if (strcmp(str, "#setname") == 0)
	{
		pthread_mutex_lock(&mutx);
		set_name(strtok(NULL, sep), sock);
		pthread_mutex_unlock(&mutx);
		
		for (int i = 0; i < clnt_cnt; i++)
		{
			printf("%d %s %d\n", clnt_socks[i], clnt_name[i], clnt_room[i]);
		}
	}
	// 방 생성 메시지 수신 시.
	else if(strcmp(str, "#makeroom")==0)
	{
		pthread_mutex_lock(&mutx);
		make_room(strtok(NULL, sep), sock);
		pthread_mutex_unlock(&mutx);
	}
	// 방 입장 메시지 수신 시.
	else if (strcmp(str, "#enterroom") == 0)
	{
		pthread_mutex_lock(&mutx);
		enter_room(strtok(NULL, sep), sock);
		pthread_mutex_unlock(&mutx);
	}
	// 방 내부 메시지 수신 시.
	else if (strcmp(str, "#chatroom") == 0)
	{
		pthread_mutex_lock(&mutx);
		chat_room(strtok(NULL, sep), sock);
		pthread_mutex_unlock(&mutx);
	}
	// 기타 수신시엔 에코서버 기능.
	else
	{
		return_msg(str, sock);
	}
}

/*
이름 설정 함수. 
입력 : 이름(char*), 소켓 파일 디스크립터(int)
*/
void set_name(char* name, int sock)
{
	int sock_index;
	// 소켓 인덱스 찾기
	for (sock_index = 0; sock_index < clnt_cnt; sock_index++)
	{
		if (sock == clnt_socks[sock_index])
		{
			break;
		}
	}

strcpy(clnt_name[sock_index], name);
printf("%s is connected.\n", clnt_name[sock_index]);
}

/*
방 생성 함수.
입력 : 방 이름(char*), 소켓 파일 디스크립터(int)
*/
void make_room(char* room_data, int sock)
{
	int sock_index;
	char temp_pass[300];
	char* room_name;
	char* room_pass;
	char* room_option;
	char* temp_str;
	int i;

	room_option = strtok(room_data, "$$");
	room_name = strtok(NULL, "$$");
	if (!strcmp(room_option, "Y") || !strcmp(room_option, "y")){
		room_name = strtok(room_name, "&&");
		room_pass = strtok(NULL, "&&");
	}
	printf("room_option %s , room_name %s, room_pass %s\n", room_option, room_name, room_pass);

	// 소켓 인덱스 찾기
	for (sock_index = 0; sock_index < clnt_cnt; sock_index++)
	{
		if (sock == clnt_socks[sock_index])
		{
			break;
		}
	}

	for (i = 0; i < room_cnt; i++)
	{
		if (strcmp(room_name, rooms[i]) == 0) // 이미 방이 존재할 때.
		{
			if (room_set[i].set_secret == 1) 
			{
				if (!strcmp(room_option, "Y") || !strcmp(room_option, "y")) {
					if (strcmp(room_set[i].pass_word, room_pass))
					{
						printf("Password Error\n");
						write(sock, "Password Error\n", strlen("Password Error\n"));
						break;
					}
				}
				else
				{
					write(sock, "Please Password input : ", strlen("Please Password input : "));  //여기 문제.
					read(sock, temp_pass, sizeof(temp_pass));
					if (strcmp(room_set[i].pass_word, temp_pass))
					{
						printf("Password Error\n");
						write(sock, "Password Error\n", strlen("PassWord Error\n"));
						break;
					}
					
				}
			}
			clnt_room[sock_index] = i;
			temp_str = "Connecting to existing room.\n";
			printf("%s", temp_str);
			write(sock, temp_str, strlen(temp_str));
			room_set[clnt_room[sock_index]].person_num++;

			// 접속 메시지를 같은 방의 다른 사람들에게 보내줌.
			connect_msg(sock_index);
			break;
		}
		else if (i == room_cnt - 1) // 방이 없을 때. 새로 생성.
		{
			strcpy(rooms[room_cnt], room_name);
			clnt_room[sock_index] = room_cnt++;
			room_set[clnt_room[sock_index]].person_num = 1;
			if (!strcmp(room_option, "Y") || !strcmp(room_option, "y")) //비밀번호 방일시 패스워드 저장.
			{
				room_set[clnt_room[sock_index]].set_secret = 1;
				strcpy(room_set[clnt_room[sock_index]].pass_word, room_pass);
			}
			temp_str = "New room is created.\n";
			printf("%s", temp_str);
			write(sock, temp_str, strlen(temp_str));
			break;
		}
	}
	if (room_cnt == 0) // room_cnt = 0일 때 처리가 안 될 거 같아서 따로 뺌.
	{
		strcpy(rooms[room_cnt], room_name);
		clnt_room[sock_index] = room_cnt++;
		room_set[clnt_room[sock_index]].person_num = 1;
		if (!strcmp(room_option, "Y") || !strcmp(room_option, "y")) //비밀번호 방일시 패스워드 저장.
		{
			room_set[clnt_room[sock_index]].set_secret = 1;
			strcpy(room_set[clnt_room[sock_index]].pass_word, room_pass);
		}
		temp_str = "New room is created.\n";
		write(sock, temp_str, strlen(temp_str));
	}
}

/*
방 입장 함수.
입력 : 방 이름(char*), 소켓 파일 디스크립터(int)
*/
void enter_room(char* room_name, int sock)
{
	int sock_index;
	char temp_pass[300];
	char* temp_str;
	int i,pass_len;

	// 소켓 인덱스 찾기.
	for (sock_index = 0; sock_index < clnt_cnt; sock_index++)
	{
		if (sock == clnt_socks[sock_index])
		{
			break;
		}
	}

	// 클라이언트에서 전송받은 이름을 가진 방 찾기.
	for (i = 0; i < room_cnt; i++)
	{
		printf("%d, roomname-%s:%s\n", room_cnt, room_name, rooms[i]);
		if (strcmp(room_name, rooms[i]) == 0) // 이미 방이 존재할 때.
		{
			if (room_set[i].set_secret == 1)
			{
				write(sock, "Please Password input : ", strlen("Please Password input : "));  //여기 문제.
				pass_len=read(sock, temp_pass, sizeof(temp_pass));
				temp_pass[pass_len] = 0;
				printf("%s\n", temp_pass);
				if (strcmp(room_set[i].pass_word, temp_pass))
				{
					printf("Password Error\n");
					write(sock, "Password Error\n", strlen("PassWord Error\n"));
					break;
				}
			}
			clnt_room[sock_index] = i;
			temp_str = "Connecting to existing room.\n";
			printf("%s", temp_str);
			write(sock, temp_str, strlen(temp_str));
			room_set[clnt_room[sock_index]].person_num++;

			// 접속 메시지를 같은 방의 다른 사람들에게 보내줌. 
			connect_msg(sock_index);
			break;
		}
		
		else if (i == room_cnt - 1) // 방이 없을 때. 접속 실패했다고 보내줌.
		{
			temp_str = "Can't find a room.\n";
			printf("%s", temp_str);
			write(sock, temp_str, strlen(temp_str));
			break;
		}
	}
	if (room_cnt == 0)
	{
		temp_str = "Can't find a room.\n";
		//sprintf(temp_str, "There isn't room named as '%s'.\n", room_name);
		printf("%s", temp_str);
		write(sock, temp_str, strlen(temp_str));
	}
}

/*
방 내부 채팅 함수.
입력 : 메시지(char*), 소켓 파일 디스크립터(int)
*/
void chat_room(char* msg, int sock)
{
	int sock_index;
	int i,check=0;
	char* ear_msg;
	char temp_save[BUF_SIZE + NAME_SIZE + 5];  //수정된 부분
	char temp_str[BUF_SIZE + NAME_SIZE + 5];
	/*
	char temp_self_str[200];
	char temp2_str[BUF_SIZE + NAME_SIZE + 5];
	char upper_box[50] = "┌━━━━━━━━━━━━━━━━━━━━━━━━━━━━┐\n";
	char under_box_left[50] = "└ ───────────────────────────┘\n";
	char under_box_right[10] = "└─────────────────────────── ┘\n";
	char under_tail_left[10] = " │/\n";
	char under_tail_right[10] = "\\│ \n";
	*/

	memset(temp_str, 0, sizeof(temp_str));
	memset(temp_save, 0, sizeof(temp_save));
	strcpy(temp_save, msg);
	ear_msg = strtok(temp_save, " ");

	// 소켓 인덱스 찾기
	for (sock_index = 0; sock_index < clnt_cnt; sock_index++)
	{
		if (sock == clnt_socks[sock_index])
		{
			break;
		}
	}
	if (!strcmp(ear_msg, "/r") == 0) {
		// 클라이언트가 채팅방을 나갈 경우
		if (strcmp(msg, "q\n") == 0 || strcmp(msg, "Q\n") == 0) 
		{
			sprintf(temp_str, "%s go out of a chatroom\n", clnt_name[sock_index]);
			check = 1; //나갈상태라는것을 체크해둠
		}
		// 클라이언트에게 전송할 [이름]:메시지 문자열 생성.
		else
		{
			/*
			sprintf(temp2_str, "│[%s] %s", clnt_name[sock_index], msg);
			sprintf(temp_str, "%s%s%s%s", upper_box, temp2_str, under_box_left, under_tail_left);
	
			sprintf(temp2_str, "[%s] %s│", clnt_name[sock_index], msg);
			sprintf(temp_self_str, "%45s%45s%45s%45s", upper_box, temp2_str, under_box_right, under_tail_right);
		 	for (i = 0; i < clnt_cnt; i++)
	                {
        	                printf("%d : %d\n", clnt_room[i], clnt_room[sock_index]);
                	        if (clnt_room[i] == clnt_room[sock_index]
                        	        && i == sock_index)
                       		{
                                	write(clnt_socks[i], temp_self_str, strlen(temp_str));
                       		}	
                	}
			*/
			sprintf(temp_str, "[%s]:%s", clnt_name[sock_index], msg);

		}
		// 자기자신이 아니면서 같은 방에 있는(clnt_room[i]가 같은) 클라이언트에게 전송
		for (i = 0; i < clnt_cnt; i++)
		{
			printf("%d : %d\n", clnt_room[i], clnt_room[sock_index]);
			if (clnt_room[i] == clnt_room[sock_index]
				&& i != sock_index)
			{
				write(clnt_socks[i], temp_str, strlen(temp_str));
			}
		}
	}
	else //귓속말일 경우.
	{
		ear_msg = strtok(NULL, " ");
		printf("%s", ear_msg);
		for (i = 0; i < clnt_cnt; i++)  //같은 방에 있는 귓속말할 상대를 찾는다.
		{
			printf("%d : %d\n", clnt_room[i], clnt_room[sock_index]);
			if (clnt_room[i] == clnt_room[sock_index]
				&& i != sock_index && !strcmp(clnt_name[i],ear_msg))
			{
				ear_msg = strtok(NULL, " ");
				sprintf(temp_str, "[%s]:%s", clnt_name[sock_index], ear_msg);
				ear_msg = strtok(NULL, " ");
				while (ear_msg != NULL)
				{
					strcat(temp_str, " ");
					strcat(temp_str, ear_msg);
					ear_msg = strtok(NULL, " ");
				}
				write(clnt_socks[i], temp_str, strlen(temp_str));
				break;
			}
		}
		if (i == clnt_cnt)
			write(sock, "Name Error\n", strlen("Name Error\n"));
	}
	if (check == 1) {
		room_set[clnt_room[sock_index]].person_num--;
		if (room_set[clnt_room[sock_index]].person_num == 0) // 서버에서 채팅방을 삭제한다.
		{
			room_set[clnt_room[sock_index]].set_secret = 0;
			printf("%s chatroom deleting\n", rooms[clnt_room[sock_index]]);
			for (i = clnt_room[sock_index]; i < room_cnt; i++)
				strcpy(rooms[i], rooms[i + 1]);
			room_cnt--;
		}
		clnt_room[sock_index] = -1;
	}
}

/*
방의 멤버들에게 새로운 멤버의 접속을 알리는 함수.
입력 : 소켓 인덱스(int)
*/
void connect_msg(int sock_index)
{
	int i;
	char temp_str[BUF_SIZE + NAME_SIZE + 5];
	memset(temp_str, 0, sizeof(temp_str));

	sprintf(temp_str, "[%s] is connected.\n", clnt_name[sock_index]);

	// 자기자신이 아니면서 같은 방에 있는(clnt_room[i]가 같은) 클라이언트에게 전송
	for (i = 0; i < clnt_cnt; i++)
	{
		printf("%d : %d\n", clnt_room[i], clnt_room[sock_index]);
		if (clnt_room[i] == clnt_room[sock_index]
			&& i != sock_index)
		{
			write(clnt_socks[i], temp_str, strlen(temp_str));
		}
	}

	return;
}

// Echo Server 기능. 클라이언트에게 받은 메시지 되돌려 줌.
// 클라이언트에게서 받는 건 메시지 뿐으로 서버가 [이름]:메시지 문자열을 생성해서 전송.
void return_msg(char * msg, int sock)
{
	int sock_index;
	char temp_str[BUF_SIZE+NAME_SIZE+5];
	printf("%s\n", msg);
	memset(temp_str, 0, sizeof(temp_str));

	for (sock_index = 0; sock_index < clnt_cnt; sock_index++)
	{
		if (sock == clnt_socks[sock_index])
		{
			break;
		}
	}

	sprintf(temp_str, "[%s]: %s\n", clnt_name[sock_index], msg);
	write(sock, temp_str, strlen(temp_str));
}
// CTL+C 입력시 모든 클라이언트를 닫고 서버를 종료시킨다.
void keycontrol(int sig)
{
	int i;
	char buf[100]="Server forced termination.\n";

	if (sig == SIGINT)
		printf("Server forced termination.\n");
	for (i = 0; i < clnt_cnt; i++) {
		write(clnt_socks[i], buf, strlen(buf));
		close(clnt_socks[i]);
	}
	close(serv_sock_save);
	exit(1);
}
void error_handling(char * msg)
{
	fputs(msg, stderr);
	fputc('\n', stderr);
	exit(1);
}

