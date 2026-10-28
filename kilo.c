/* INCLUDES */
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <ctype.h>

//CTRL_KEY é uma função macro que pega um argumento k (que normalmente é um caractere)
//o (k) & 0x1f pelo que eu entendi, pega o valor de ASCII do k e faz uma operação de bitwise AND com o valor 0x1f
//ele basicamente pega qualquer tecla que você apertar e transforma em código de controle correspondente ao apertar <ctrl + k ou qlqr outra tecla>
#define CTRL_KEY(k) ((k) & 0x1f)

#define KILO_VERSION "0.0.1"



//Atributos do terminal podem ser lidos e modificados usando a struct termios
struct editorConfig{
	int screenrows;
	int screencols;
	struct termios org_termios;
};

struct editorConfig E;

/* APPEND BUFFER */
//o append buffer vai servir de ponteiro para o nosso buffer na memoria e o tamanho dele
struct abuf {
	char *b;
	int len;
};
//representa um buffer vazio
//ler rascunho [5]
#define ABUF_INIT {NULL, 0};

void abAppend(struct abuf *ab, const char *s, int len){
	//basicamente aqui estamos redimensionando o tamanho do buffer para aumentar seu tamanho, a sintaxe ab->b é que estamos acessando a variavel b através
	//de um ponteiro. a função realloc recebe um buffer e o tamanho novo.
	char *new = realloc(ab->b, ab->len + len);

	if(new == NULL) return;
	//memcpy (memory copy) copia n bytes de um endereço de memoria x até um destino y, abaixo estamos copiando o que está em s para o new e o len é a quantidade de bytes
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void abFree(struct abuf *ab){
	free(ab->b);
}
//função que imprime um erro e fecha o programa
//ler rascunho [4]
void die(const char *s){
	//Estamos limpando a tela mas com o comando \x1b[2J 
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
	perror(s);
	exit(1);
}

/* TERMINAL */
void disableRawMode(){
	//retorna -1 se houver algum erro
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH , &E.org_termios) == -1 )
		die("tcsetattr");
}


void enableRawMode(){
	//lemos a configuração atual
	//retorna -1 se houver algum erro
	if(tcgetattr(STDIN_FILENO, &E.org_termios) == -1 )
		die("tcgetattr");
	//desabilitamos o modo raw ao final da execução do programa
	atexit(disableRawMode);


	struct termios raw = E.org_termios;
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

char editorReadKey(){
	int nread;
	char c;
	//testa se um caracter é um control caracter, que não queremos imprimir (olhe a tabela ASCII) 
	//retorna -1 se houver algum erro
	//basicamente esse EAGAIN só tá ai caso esse editor rode em um cygwin
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if(nread == -1 && errno != EAGAIN) 
			die("read");
	}

	return c;
}

//função que coloca o seu cursor no canto inferior direito para calcular o tamanho do terminal
int getCursorPosition(int *rows, int *cols){
	char buf[32];
	unsigned int i = 0;

	if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
  		return -1; 

	printf("\r\n");

	while(i < sizeof(buf) - 1){
		if(read(STDIN_FILENO, &buf[i], 1) != 1) 
			break;

		if(buf[i] == 'R') 
			break;

		i++;
	}

	buf[i] = '\0';
	//verificamos se a string impressa começa com uma escape sequence
	if(buf[0] != '\x1b' || buf[1] != '[') 
		return -1;
	//sscanf é uma função de escanear e formatar strings, no caso vamos ler a partir do terceiro caracter e ler a parte que corresponde ao formato %d;%d
	//e colocar esses valores nas variáveis rows e cols
	if(sscanf(&buf[2], "%d;%d", rows, cols) != 2)
		return -1;

	
	return 0;
}

int getWindowSize(int *rows, int *cols){
	struct winsize ws;
	//usamos a função ioctl pra retornar o tamanho da tela do terminal usando o parametro TIOCGWINSZ(Terminal Input Output Control Get Windows Size)
	if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){

		if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
			return -1;


		return getCursorPosition(rows, cols);
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}
/* OUTPUT */
//Função para imprimir o char '~' no começo de cada linha igual no vim
void editorDrawRows(struct abuf *ab){
	int y;
	for(y = 0; y < E.screenrows; y++) {
		if(y == E.screenrows / 3){
			char welcome[80];
			int welcomelen = snprintf(welcome, sizeof(welcome),"Kilo editor -- version %s", KILO_VERSION);
			if(welcomelen > E.screencols) welcomelen = E.screencols;
		    abAppend(ab, welcome, welcomelen);
		}else {
			abAppend(ab, "~", 1);
		}
		//comando para limpar uma linha por vez na tela
		abAppend(ab, "\x1b[K", 3);
		if(y < E.screenrows - 1){
			abAppend(ab, "\r\n", 2);
		}
	}
}

void editorRefreshScreen() {
	struct abuf ab = ABUF_INIT;

	//comando para esconder o cursor
	abAppend(&ab, "\x1b[?25l", 6);	
	abAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);

	abAppend(&ab, "\x1b[H", 3);
	abAppend(&ab, "\x1b[?25h", 6);	
	
	write(STDOUT_FILENO,ab.b , ab.len);
	abFree(&ab);
}



/* INPUT */
void editorProcessKeypress(){
	char c = editorReadKey();

	switch(c) {
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;
	}
}

/* INIT */
void initEditor(){
	if(getWindowSize(&E.screenrows, &E.screencols) == -1)
		die("getWindowSize");
}
int main(){
	enableRawMode();
	initEditor();
	while(1){
		editorRefreshScreen();
		editorProcessKeypress();
	}
	

	return 0;
}
