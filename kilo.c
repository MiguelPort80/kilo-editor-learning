#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <stdio.h>
#include <ctype.h>

//Atributos do terminal podem ser lidos e modificados usando a struct termios
struct termios org_termios;

//função que imprime um erro e fecha o programa
//ler rascunho [4]
void die(const char *s){
	perror(s);
	exit(1);
}

void disableRawMode(){
	//retorna -1 se houver algum erro
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH , &org_termios) == -1 )
		die("tcsetattr");
}


void enableRawMode(){
	//lemos a configuração atual
	//retorna -1 se houver algum erro
	if(tcgetattr(STDIN_FILENO, &org_termios) == -1 )
		die("tcgetattr");
	//desabilitamos o modo raw ao final da execução do programa
	atexit(disableRawMode);


	struct termios raw = org_termios;
	// ler rascunho [2]
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);   
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag &= ~(CS8);
	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	//setamos as configurações, o argumento TCSAFLUSH especifíca quando aplicar a mudança, nesse caso espera todo o output pendente para ser escrito
	//no terminal, retorna -1 se houver algum erro
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH , &raw) == -1 )
		die("tcsetattr");
}
int main(){
	enableRawMode();
	while(1){
		char c;
		//retorna -1 se houver algum erro
		//basicamente esse EAGAIN só tá ai caso esse editor rode em um cygwin
		if(read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN)
			die("read");
		//testa se um caracter é um control caracter, que não queremos imprimir (olhe a tabela ASCII) 
		if(iscntrl(c)){
			printf("%d\r\n", c);
		}else {
			printf("%d ('%c')\r\n",c ,c);
		}
		if(c == 'q') break;
	}
	

	return 0;
}
