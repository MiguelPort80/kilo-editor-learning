#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <stdio.h>
#include <ctype.h>

//Atributos do terminal podem ser lidos e modificados usando a struct termios
struct termios org_termios;

void disableRawMode(){
	tcsetattr(STDIN_FILENO, TCSAFLUSH , &org_termios);
}


void enableRawMode(){
	//lemos a configuração atual
	tcgetattr(STDIN_FILENO, &org_termios);
	//desabilitamos o modo raw ao final da execução do programa
	atexit(disableRawMode);


	struct termios raw = org_termios;
	// ler rascunho [2]
	raw.c_iflag &= ~(BRKINT | ICRLN | INPCK | ISTRIP | IXON);   
	raw.o_lflag &= ~(OPOST);
	raw.c_cflag &= ~(CS8);
	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

	//setamos as configurações, o argumento TCSAFLUSH especifíca quando aplicar a mudança, nesse caso espera todo o output pendente para ser escrito
	//no terminal
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main(){
	enableRawMode();
	char c;
	while(read(STDIN_FILENO, &c, 1) == 1 && c != 'q'){
		//testa se um caracter é um control caracter, que não queremos imprimir (olhe a tabela ASCII) 
		if(iscntrl(c)){
			printf("%d\r\n", c);
		}else {
			printf("%d ('%c')\r\n",c ,c);
		}
	}
	

	return 0;
}
