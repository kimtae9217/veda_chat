# TCP를 이용한 멀티 프로세스 기반 채팅 프로그램

### 미니 프로젝트 기간 :  24.9.11 ~ 24.9.13

## 프로젝트 개요

- **프로젝트 환경**
	- 사용 언어 :  C
	- 개발 환경 :  RaspberryPi 5 , Linux
	
- **요구 사항**
	- TCP 통신
	- IPC 통신으로 pipe()만 사용
	- 서버와 클라이언트는 소켓 통신 사용
	- 서버는 멀티프로세스(fork()) 사용
	- 채팅 서버는 데몬으로 등록

- **기능 명세**
	- Server는 1:1  or 1:N 채팅 기능 제공 (다중 클라이언트 접속 가능)
	- Server는 최대 50개의 Client에 대해 접속 허용
	- 로그인, 로그아웃 기능
	- 채팅 시 로그인 된 ID 뒤에 메시지 표시  
		- ex) [name] : hello

- **제약 사항**
	- Select() / epoll() 함수 사용 불가
	- 메세지 큐나 공유 메모리 사용 불가
	- 스레드 사용 불가


## 🛠️ Architecture
  ![image](https://github.com/user-attachments/assets/9b5b29ab-fc06-4fec-89c5-0b1750cbff74)

## Manual

- Install
```bash
sudo apt-get update
sudo apt-get install libncurses5-dev libncursesw5-dev
```

- Complie (Makefile)
```bash
make
```
or
```bash
gcc -o server server.c
gcc -o client client.c -lncurses
```

- Start

한 개의 터미널을 켜서 실행 (Server)
```bash
./server
```
또 하나의 터미널을 켜서 실행 (Client)
```bash
./client 127.0.0.1 5100
```
</br>

## Daemon Server
데몬 서버이기 때문에 실행하고 난 후 아무것도 뜨지 않음. 아래와 같은 명령어로 데몬서버가 실행 중인지 확인
```bash
ps aux | grep server
```
## 결과 화면 (Client)
![image](https://github.com/user-attachments/assets/c728e0a5-e072-4d16-83ed-4d7cd733cf44)

