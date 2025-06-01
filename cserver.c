#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdlib.h>
#include <poll.h>

#define NODES 16
#define MAX_MSG 10

struct chat {
	char msg[64];
	int sock;
};

struct chat msg_queue[MAX_MSG];  //チャットに表示する文字を保存する配列
struct chat* ep;  //msg_queueの次に入れる変数を指すポインタ
struct pollfd fds[NODES];  //poll()で監視するファイルディスクリプタの配列

//接続されているソケットの数を返す関数
int get_fd_connection() {
	int count = 0;
	struct pollfd* fd = fds;
	while (fd->fd != 0) {
		if (count == NODES-1) {
			printf("max NODES,can't read\n");
			exit(1);
		}
		fd++;
		count++;
	}
	return count;
}

//受け取ったソケットのfdsのオフセットを返す関数
int get_fd_index(int socket) {
	int count = 0;
	struct pollfd* fd = fds;
	while (fd->fd != 0) {
		if (count == NODES-1) {
			printf("get_fd_index error\n");
			exit(1);
		}
		if (fd->fd == socket) return count;
		fd++;
		count++;
	}
}

//文字列を格納するキューの中で引数として受け取ったソケットを全て-1にする関数
void reset_queue(int key) {
	struct chat* cp;
	cp = ep+1;
	if (cp->sock == 0) cp = msg_queue;
	while (1) {
		if (cp == (msg_queue+MAX_MSG)) cp = msg_queue;
		if (cp->sock == key) cp->sock = -1;  //ソケットが同じ時変更
		if (cp == ep) break;
		cp++;	
	}
}


//受け取った文字列をチャンク化して送信する関数
void send_string(char* str, int sockfd) {
	char len[8];
	sprintf(len, "%x", strlen(str));
	strcat(len, "\r\n");
	strcat(str, "\r\n");
	if (send(sockfd, len, strlen(len), 0) == -1) {  //チャンクの長さを16進数の形式で送信
		perror("send");
		exit(1);
	}
	if (send(sockfd, str, strlen(str), 0) == -1) {  //文字列を送信
		perror("send");
		exit(1);
	}
}

//チャットに表示する文字列を全て送信する関数
void send_queue(int sockfd) {
	char msg[128], name[128], msg_id[16], name_id[16], color[16];
	int i = 0, name_val;
	struct chat *cp, *finish;
	cp = ep;
	if (cp->sock == 0) {  
		if (cp == msg_queue) return;
		finish = msg_queue;
	} else {
		finish = ep;
	}
	while (1) {  //新しい順に下から表示
		cp--;
		i++;
		if (cp < msg_queue) cp = msg_queue+MAX_MSG-1;
		name_val = get_fd_index(cp->sock);  //名前の識別子としてfdsのオフセットを取得
		if (cp->sock == sockfd) {  //自身が送信したメッセージを左側に表示
			strcpy(msg_id, "left_msg");
			strcpy(name_id, "left_name");
			strcpy(color, "black");
		} else {  //他人が送信したメッセージを右側に表示
			strcpy(msg_id, "right_msg");
			strcpy(name_id, "right_name");
			strcpy(color, "right_name");
			if (cp->sock == -1) {  //通信が切れている場合
				strcpy(color, "lightgray");   //メッセージをグレーにする
				name_val = 0;  //名前を0にする
			} else {
				strcpy(color, "black");
			}
		}
		sprintf(name, "<p class='name' id='%s' style='top: %dvh;'>name_%d</p>", name_id, 80-i*8, name_val);  //html形式で名前を保存
		sprintf(msg, "<p class='msg' id='%s' style='color:%s; top: %dvh'>%s</p>", msg_id, color,80-i*8, cp->msg);  //html形式でメッセージを保存
		send_string(name, sockfd);
		send_string(msg, sockfd);
		if (cp == finish) break;
	}
}

//引数に受け取ったソケットを閉じる関数
void delete_fd(int key) {
	int flag = 0;
	int count = 0;
	struct pollfd* fd = fds;
	
	while(fd->fd != 0) {
		if (fd->fd == key) {  //閉じるソケットのときcloseする
			if (close(fd->fd) == -1) {
				perror("close sockfd");
				exit(1);
			}
			flag = 1;
			reset_queue(fd->fd);  //文字列を保存するキューに対応していたソケットを−１に設定
			printf("socket is deleted\n");
		}
		if (flag == 1) {  //ソケットをクローズした後ろのソケットを詰める
			fd->fd = (fd+1)->fd;
			fd->events = (fd+1)->events;		
		}
		if (count == NODES-1) {
			printf("max NODES, can't delete\n");
			exit(1);
		}
		count++;
		fd++;
	}
	fd--;
	fd->fd = 0;
}

//監視するファイルディスクリプタの配列にソケットを追加する関数
void add_fd(int socket) {
	int count = 0;
	struct pollfd* fd = fds;
	while (fd->fd != 0) {
		if (count == NODES-1) {
			printf("max NODES,can't add\n");
			exit(1);
		}
		fd++;
		count++;
	}
	fd->fd = socket;  //ソケットを追加
	fd->events = POLLIN;  //読み込み可能かを監視するイベントを追加
	printf("socket is added\n");
}

char *get_filename(char *buff) {
	if (*buff == '/') {
		char *p, *fname;
		buff++;
		p = strchr(buff, ' ');
		if (p == 0) {printf("Illegal format\n"); exit(1);}
		*p = '\0';
		if (p==buff) strcpy(buff, "index.html");
		fname = malloc(strlen(buff)+1);
		strcpy(fname, buff);
		return fname;
	} else {
		printf("Unknown header %s\n", buff);
		exit(1);
	}
}

//pollで呼び出し読み込み可能データが見つかったとき、そのソケットの配列を調べる関数
int check_poll(int sockfd, int val, int* temp_fd) {
	int new_sockfd, count = 0;
	for (int i=0; i<NODES; i++) {
		if (fds[i].revents & POLLERR) {  //ソケットがエラーになったとき
			printf("POLLERR\n");
			exit(1);
		}
		if (fds[i].revents & POLLIN) {  //ソケットが読み込み可能になったとき
			if (i == 0) {  //新規接続要求があるとき
				new_sockfd = accept(sockfd, NULL, NULL);  //接続受付
				if (new_sockfd == -1) {
					perror("accept");
					exit(1);
				}
				add_fd(new_sockfd);  //新規ソケットを登録する
				temp_fd[count] = new_sockfd; 
			} else {  //既存のソケットでの通信のとき
				printf("existing socket is called\n");
				temp_fd[count] = fds[i].fd;
			}
			count++;
			if (count >= val) return 0;  //読み込み可能データ数を超えるとき終了
		}
	}
	return -1;
}

//リクエストを読み込み対応したレスポンスを返す関数
void response(int sockfd) {
	FILE *fp;
	int content_len, val;
	char rcv_data[1024], send_data[64], *file_name;
	
	//リクエスト受信
	val = recv(sockfd, rcv_data, 2048, 0);  //リクエストを受け取る
	if (val == -1) {
		perror("recv");
		exit(1);
	}
	if (val == 0) {  //読み込み可能データがないとき、相手の接続が閉じていると判断
		delete_fd(sockfd);  //対応するソケットを閉じる
		return;
	}
	printf("%s\n", rcv_data);
	if (!strncmp(rcv_data, "GET", 3)) {  //GETで送られるとき
		file_name = get_filename(rcv_data+4);
		if (!strcmp(file_name, "favicon.ico")) return;  //favicon.icoが送られるとき無視する
	} else if (!strncmp(rcv_data, "POST", 4)) {  //POSTで送られるとき
		file_name = get_filename(rcv_data+5);
		char* str = strstr(rcv_data+20, "Content-Length:");  //Content-Lengthヘッダを抜き出す
		content_len = atoi(str+16);  //ボディの長さを抜き出す
		if (content_len != 0) {  //コメントが送られてきたとき
			char* msg = strstr(str, "addr=");  //送られた内容を受け取る
			*(msg+content_len) = '\0';	
			strcpy(ep->msg, msg+5);
			ep->sock = sockfd;  //文字列キューに内容を登録
			ep++;
			if (ep == (msg_queue+MAX_MSG)) ep = msg_queue;
		}
	}
	
	//レスポンス送信
	fp = fopen(file_name, "r+");  //form.htmlを開く
	if (fp == NULL) {
		perror("fopen");
		exit(1);
	}
	char header[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nTransfer-Encoding: chunked\r\nConnection: keep-alive\r\n\r\n";
	if (send(sockfd, header, strlen(header), 0) == -1) {  //レスポンスヘッダを送る
		perror("send");
		exit(1);
	}
	while (1) {
		if (fgets(send_data, 1024, fp) == 0) break;  //form.htmlから1文ずつ読み込む
		char *p = strchr(send_data, '\n');
		if (p!=0) *p = '\0';
		send_string(send_data, sockfd);  //読み込んだデータを送信
		if (!strcmp(send_data, "\t\t\t\t<div id='icon'></div>\r\n")) {  //ある特定の箇所までhtmlを読み込んだとき
			sprintf(send_data, "<p id='participant'>%d</p>", get_fd_connection()-1);  //更新時の参加者数を表示
			send_string(send_data, sockfd);		
		}
		if (!strcmp(send_data, "\t\t\t</div>\r\n")) {  //ある特定の箇所までhtmlを読み込んだとき
			send_queue(sockfd);  //チャット内容を全て表示する
		}
	}
	send(sockfd, "0\r\n\r\n", 5, 0);  //送信を終了する
}


int main() {
	struct sockaddr_in serv_addr;
	int sockfd, flag=1, val, temp_fd[NODES];
	FILE *istream;
	
	ep = msg_queue;
	sockfd = socket(AF_INET, SOCK_STREAM, 0);  //ソケットを作成
	if (sockfd == -1) {
		perror("socket");
		exit(1);
	}
	memset(&serv_addr, 0, sizeof(struct sockaddr_in));  //serv_addrを0で初期化
	memset(fds, 0, sizeof(fds));  //fdsを0で初期化
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(80);
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int)) == -1) {
		perror("setsockopt");
		exit(1);
	}
	if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr_in)) == -1) {  //ソケットとアドレスを関連付ける
		perror("bind");
		exit(1);
	}
	if (listen(sockfd, 5) == -1) {  //コネクションの要求待ち
		perror("listen");
		exit(1);
	}
	add_fd(sockfd);  //新規接続を受け付けるソケットを登録
	while (1) {
		val = poll(fds, NODES, -1);  //読み込み可能データがあるまで待機
		if (val == -1) {
			perror("poll");
			exit(1);
		}
		if (check_poll(sockfd, val, temp_fd) == -1) {  //読み込み可能データがあるソケットを得る
			printf("check_poll error\n");
			exit(1);
		}
		for (int i=0; i<val; i++) {  //読み込み可能なソケット全てに対応する
			response(temp_fd[i]);  //リクエストを読み込み、対応したレスポンスを返す
		}
	}
	if (close(sockfd) == -1) {
		perror("close sockfd");
		exit(1);
	}
}




