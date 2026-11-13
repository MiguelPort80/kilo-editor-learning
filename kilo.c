/* INCLUDES */
//defines caso o compilador reclame do uso de getline() o gcc reclamou btw
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8
#define KILO_QUIT_TIMES 3 

//CTRL_KEY é uma função macro que pega um argumento k (que normalmente é um caractere)
//o (k) & 0x1f pelo que eu entendi, pega o valor de ASCII do k e faz uma operação de bitwise AND com o valor 0x1f
//ele basicamente pega qualquer tecla que você apertar e transforma em código de controle correspondente ao apertar <ctrl + k ou qlqr outra tecla>
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey{
	BACKSPACE = 127,
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN,
	ESC_KEY
};

enum motionKeys{
	MOTIONKEY_H = 104,
	MOTIONKEY_J = 106,
	MOTIONKEY_K = 107,
	MOTIONKEY_L = 108	
};

//struct para armazenar as linhas que o editor vai ler
typedef struct erow{
	int size;
	int rsize;
	char *chars;
	char *render;
} erow;


//Atributos do terminal podem ser lidos e modificados usando a struct termios
struct editorConfig{
	// X é a coordenada horizontal e Y é a coordenada vertical
	int cursorX, cursorY;
	int renderX;
	int rowoff;
	int coloff;
	int screenrows;
	int screencols;
	int numrows;
	erow *row;
	int dirty;
	char *filename;
	char statusmsg[80];
	time_t statusmsg_time;
	struct termios org_termios;
};

struct editorConfig E;
//Colocando os modos do vim
int mode = 0; //NORMAL MODE 0 | INSERT MODE 1

/* PROTOTIPOS */

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void(*callback)(char *, int));

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
	//escreve uma quantidade de bytes em um arquivo ou stream
	//ssize_t write(int fd, const void buf[.count], size_t count);
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

int editorReadKey(){
	int nread;
	char c;
	//testa se um caracter é um control caracter, que não queremos imprimir (olhe a tabela ASCII) 
	//retorna -1 se houver algum erro
	//basicamente esse EAGAIN só tá ai caso esse editor rode em um cygwin
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if(nread == -1 && errno != EAGAIN) 
			die("read");
	}
	//checamos se o input é um arrowkey, já que eles começam com a escape sequence '\x1b'
	if(c == '\x1b'){
		char seq[3];

		if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

		//os input arrowkey tem a escape sequence e logo após tem uma letra de identificação, aqui estamos verificando qual letra é e atribuindo a sua 
		//função as suas respectivas constantes
		if(seq[0] == '['){
			if(seq[1] >= '0' && seq[1] <= '9') {
				if(read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if(seq[2] == '~'){
					switch(seq[1]){
						case '1': return HOME_KEY;
						case '3': return DEL_KEY;
						case '4': return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY;
						case '8': return END_KEY;
					}
				}
			}else{
				switch(seq[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
				}
			}
		}else if(seq[0] == 'O'){
			switch(seq[1]){
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
			}
		}	
		return '\x1b';
	}else{
		return c;
	}
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
/* OPERAÇÕES DE ROW*/
//função que tranforma cursorX em renderX
int editorRowCxToRx(erow *row, int cx){
	int rx = 0;
	int j;
	
	for(j = 0; j < cx; j++){
		if(row->chars[j] == '\t')
			rx += (KILO_TAB_STOP - 1) - ( rx % KILO_TAB_STOP);
		rx++;
	}
	return rx;
}

//função que tranforma renderX em cursorX
int editorRowRxToCx(erow *row, int rx){
	int cur_rx = 0; 
	int cursorX;

	for(cursorX = 0; cursorX < row->size; cursorX++){
		if(row->chars[cursorX] == '\t')
			cur_rx += (KILO_TAB_STOP - 1) - (cur_rx % KILO_TAB_STOP);
		cur_rx++;

		if(cur_rx > rx) return cursorX;
	}
	return cursorX;
}
//função para copiar cada caracter do chars para o render
void editorUpdateRow(erow *row){
	int tabs = 0;
	int j;
	//contém o número de caracteres que copiamos de render
	int idx = 0;
	for(j = 0; j < row->size; j++)
		if(row->chars[j] == '\t') tabs++;

	free(row->render);
	row->render = malloc(row->size + tabs*(KILO_TAB_STOP-1) + 1);

	for(j = 0; j < row->size; j++){
		if(row->chars[j] == '\t'){
			row->render[idx++] = ' ';
			while(idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
		} else {
			row->render[idx++] = row->chars[j];
		}
	}
	row->render[idx] = '\0';
	row->rsize = idx;
}

//I REMEMBER HALLOWEEN HALLOWEEEEN HALLOWEEEEEN
//basicamente essa função pega uma string e aloca ela no nosso row do editor usando memcpy
void editorInsertRow(int at, char *s, size_t len){
	if(at < 0 || at > E.numrows) return;
	//E.row agora vai ser um array de linhas(rows) que iremos mostrar no editor
	//alocamos espaço de memória para mais uma linha
	E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
	//copiamos um espaço de memória para a nova linha
	memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at) );



	E.row[at].size = len;
	E.row[at].chars = malloc(len + 1);
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';

	//inicialização do render
	E.row[at].rsize = 0;
    E.row[at].render = NULL;	
	editorUpdateRow(&E.row[at]);

	E.numrows++;
	E.dirty++;
}
//função que vai inserir um caracter em uma linha especifica
void editorRowInsertChar(erow *row, int at, int c){
	if(at < 0 || at > row->size) at = row->size;
	row->chars 	= realloc(row->chars, row->size + 2);
	//função que copia n bytes de uma area de memória para outra
	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = c;
	editorUpdateRow(row);
	E.dirty++;
}

//função que vai remover um caracter em uma linha especifica
void editorRowDelChar(erow *row, int at){
	if(at < 0 || at >= row->size) return;
	//substitui o caractere deletado pelos caracteres na frente
	memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
	row->size--;
	editorUpdateRow(row);
	E.dirty++;
}

void editorFreeRow(erow* row){
	free(row->render);
	free(row->chars);
}

void editorDeleteRow(int at){
	if(at < 0 || at >= E.numrows) return;
	editorFreeRow(&E.row[at]);
	memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1) );
	E.numrows--;
	E.dirty++;
}

//Função que anexa uma string ao final de uma linha.
void editorRowAppendString(erow *row, char *s, size_t len){
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], s, len);
	row->size += len; 
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	E.dirty++;
}

/* OPERAÇÕES DE EDITOR */

void editorInsertChar(int c){
	if(E.cursorY == E.numrows){
		editorInsertRow(E.numrows,"",0);
	}
	editorRowInsertChar(&E.row[E.cursorY], E.cursorX, c);
	E.cursorX++;
}

void editorInsertNewLine(){
	//se o cursor estiver no começo de uma linha, ele insere uma linha em branco na linha atual e joga o conteudo para baixo
	if(E.cursorX == 0){
		editorInsertRow(E.cursorY, "", 0);
	} else{
		erow *row = &E.row[E.cursorY];
		//cria uma nova linha abaixo
		editorInsertRow(E.cursorY + 1, &row->chars[E.cursorX], row->size - E.cursorX);
		//atualiza o ponteiro da linha pois, com o realloc da função editorInserRow o endereço pode ter mudado
		row = &E.row[E.cursorY];
		//corta o tamanho da string para ela terminar exatamente aonde o cursor estava
		row->size = E.cursorX;
		row->chars[row->size] = '\0';
		editorUpdateRow(row);
	}
	E.cursorY++;
	E.cursorX = 0;
}

void editorDeleteChar(){
	if(E.cursorY == E.numrows) return;
	if(E.cursorX == 0 && E.cursorY == 0) return;


	erow *row = &E.row[E.cursorY];
	if(E.cursorX > 0) {
		editorRowDelChar(row, E.cursorX - 1);
		E.cursorX--;
	}else {
		E.cursorX = E.row[E.cursorY - 1].size;
		editorRowAppendString(&E.row[E.cursorY - 1], row->chars, row->size);
		editorDeleteRow(E.cursorY);
		E.cursorY--;
	}

}
/* FILE I/O */

//função para converter os arrays do erow para string para então poder salvar com outra funçãoj
char *editorRowsToString(int *buflen){
	int totlen = 0;
	int j;
	//adicionamos o tamanho de cada linha de texto
	for(j = 0; j < E.numrows; j++)
		totlen += E.row[j].size + 1;
	//salvamos o tamanho total em buflen
	*buflen = totlen;

	char *buf = malloc(totlen);
	char *ptr = buf;

	//copiamos o conteudo de cada linha para o ponteiro do buffer	
	for(j = 0; j < E.numrows; j++){
		memcpy(ptr, E.row[j].chars, E.row[j].size);
		ptr += E.row[j].size;
		*ptr = '\n';
		ptr++;
	}

	return buf;
}

//função para abrir e ler o conteudo de um arquivo
void editorOpen(char *filename){
	free(E.filename);
	E.filename = strdup(filename);
	FILE *fp = fopen(filename, "r");
	//isso é igual a if(fp == NULL)
	if(!fp) die("fopen");

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	//getline le uma linha do arquivo e retorna o tamanho dela
	//linelen = getline(&line, &linecap, fp);
	
	while((linelen = getline(&line, &linecap, fp)) != -1){
		while(linelen > 0 && (line[linelen - 1] == '\n' ||
								line[linelen -1] == '\r'))
			linelen--;
	

		editorInsertRow(E.numrows, line, linelen);
	}
	free(line);
	fclose(fp);
	E.dirty = 0;
}	

void editorSave(){
	//se o arquivo não existir ainda vamos criar ele
	if(E.filename == NULL) {
		E.filename = editorPrompt("Save as: %s (ESC TO CANCEL)", NULL);
		if(E.filename == NULL){
			editorSetStatusMessage("Save aborted");
			return;	
		}
	}

	int len;
	char *buf = editorRowsToString(&len);
	//flag O_RDWR(read and write), O_CREAT(cria o arquivo se ele não existir), 0644 é a permisão para o arquivo sendo esse um padrão comum
	int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
	if(fd != -1){
		//define um tamanho específico para o arquivo, se ele for maior do que esse tamanho cortaremos o conteudo extra fora, se for menor preencheremos com 0
		if(ftruncate(fd, len) != -1){
			if(write(fd,buf,len) == len){
				close(fd);
				free(buf);	
				E.dirty = 0;
				editorSetStatusMessage("%d bytes written to disk", len);
				return;
			}
		}
		close(fd);
	}
	free(buf);
	//strerror pega o errno e formata para uma string que possa ser lida por um humano, errno é uma variavel que armazena informações de erro do sistema
	editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/* FIND */

//função que implementa search
void editorFindCallback(char *query, int key){
	//por padrão o ultimo match vai ser -1
	static int last_match = -1;
	static int direction = 1;
	//sempre faremos a pesquisa para frente, a não ser que o usuário queira voltar
	if(key == '\r' || key == '\x1b'){
		last_match = -1;
		direction = 1;
		return;
	}else if(key == ARROW_RIGHT || key == ARROW_DOWN){
		direction = 1;
	}else if(key == ARROW_LEFT || key == ARROW_UP){
		direction = -1;
	}else{
		last_match = -1;
		direction = 1;
	}


	if(last_match == -1) direction = 1;
	//current vai ser o index da linha que estamos procurando
	int current = last_match;
	int i;
	for(i = 0; i < E.numrows; i++){
		current += direction;
		//faz com que o current chegar no final do arquivo ele volte para o começo e viceversa
		if(current == -1) current = E.numrows - 1;
		else if (current == E.numrows) current = 0;

		erow *row = &E.row[current];
		//verifica se query está numa linha | retorna nulo se o padrão não bater
		//char *strstr(const char *haystack, const char *needle);
		char *match = strstr(row->render, query);
		if(match){
			last_match = current;
			E.cursorY = current;
			E.cursorX = editorRowRxToCx(row, match - row->render);
			E.rowoff = E.numrows;
			break;
		}
	}
}
//a função foi divida em dois para termos incremental search.
void editorFind(){
	int saved_cursorX = E.cursorX;
	int saved_cursorY = E.cursorY;
	int saved_coloff = E.coloff;
	int saved_rowoff = E.rowoff;

	char *query = editorPrompt("Search: %s (ESC/ARROWS/ENTER)", editorFindCallback);
	if(query){	
		free(query);
	}else{
		//restaurando a posição original do cursor quando não achamos a pesquisa;
		E.cursorX = saved_cursorX;
		E.cursorY = saved_cursorY;
		E.coloff = saved_coloff;
		E.rowoff = saved_rowoff;
	}

	
}


/* OUTPUT */

void editorScroll(){
	E.renderX = 0;
	if(E.cursorY < E.numrows){
		E.renderX = editorRowCxToRx(&E.row[E.cursorY], E.cursorX);
	}

	//checa se o cursor está acima da tela visível e se estiver sobe a tela também
	if(E.cursorY < E.rowoff){
		E.rowoff = E.cursorY;
	}
	//checa se o cursos está abaixo da tela visível e se estiver desce a tela
	if(E.cursorY >= E.rowoff + E.screenrows){
		E.rowoff = E.cursorY - E.screenrows + 1;
	}

	if(E.renderX< E.coloff){
		E.coloff = E.renderX;
	}

	if(E.renderX >= E.coloff + E.screencols){
		E.coloff = E.renderX - E.screencols + 1;
	}
}

//Função para imprimir o char '~' no começo de cada linha igual no vim
void editorDrawRows(struct abuf *ab){
	int y;
	for(y = 0; y < E.screenrows; y++) {
		int filerow = y + E.rowoff;
		if(filerow >= E.numrows){
			//imprime a mensagem de welcome
			if(E.numrows == 0 && y == E.screenrows / 3){
				char welcome[80];
				int welcomelen = snprintf(welcome, sizeof(welcome),"Kilo editor -- version %s", KILO_VERSION);
				if(welcomelen > E.screencols) welcomelen = E.screencols;
				//para centralizar uma string dividimos a tela por dois e subtraimos a metade do tamanho das strings a partir do valor da tela
				int padding = (E.screencols - welcomelen) / 2;
				//imprime o caracter ~ igual no vim
				if(padding) {
					abAppend(ab, "~", 1);
					padding--;
				}
				//uma das linhas de código já escritas
				//estamos preenchendo os espaços que faltam para ficar centralizado com strings vazias
				while (padding--) abAppend(ab, " ", 1);
				abAppend(ab, welcome, welcomelen);
			}else {
				abAppend(ab, "~", 1);
			}
		}else{
			//imprime as linhas no editor
			int len = E.row[filerow].rsize - E.coloff;
			if (len < 0) len = 0;
			if (len > E.screencols) len = E.screencols;
			abAppend(ab, &E.row[filerow].render[E.coloff], len);
		}
			//comando para limpar uma linha por vez na tela
			abAppend(ab, "\x1b[K", 3);
			abAppend(ab, "\r\n", 2);
		}
	}


void editorDrawStatusBar(struct abuf *ab){
	//comando que inverte a cor do terminal
	abAppend(ab, "\x1b[7m", 4);
	char status[80], rstatus[80];
	int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", 
			E.filename ? E.filename : "[No Name]", 
			E.numrows, E.dirty ? "(modified)" : "");

	int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cursorY + 1, E.numrows);
	if(len > E.screencols) len = E.screencols;
	abAppend(ab,status,len);	
	while(len < E.screencols){
		if(E.screencols - len == rlen){
			abAppend(ab,rstatus,rlen);
			break;
		}else{
			abAppend(ab, " ", 1);
			len++;
		}
	}
	abAppend(ab, "\x1b[m", 3);
	abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab){
	abAppend(ab, "\x1b[K", 3);
	int msglen = strlen(E.statusmsg);
	if(msglen > E.screencols) msglen = E.screencols;
	//faz com que a mensagem fique disponível por apenas cinco segundos
	if(msglen && time(NULL) - E.statusmsg_time < 5)
		abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen() {
	editorScroll();
	struct abuf ab = ABUF_INIT;

	//comando para esconder o cursor
	abAppend(&ab, "\x1b[?25l", 6);	
	abAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);
	editorDrawStatusBar(&ab);
	editorDrawMessageBar(&ab);
	
	//Movemos o cursor para o canto superior esquerdo da tela
	char buf[32];
	//usamos o comando H e especificamos a posição que queremos que o cursor vá usando %d
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cursorY - E.rowoff) + 1, (E.renderX - E.coloff) + 1);
	abAppend(&ab, buf, strlen(buf));


	abAppend(&ab, "\x1b[?25h", 6);	
	
	write(STDOUT_FILENO,ab.b , ab.len);
	abFree(&ab);
}

//função que vai imprimir uma mensagem do nosso editor para o usuário, funciona parecido com as funções printf
//função variatica, usamos va_list para listar os argumentos e va_start para começar a usar eles.
void editorSetStatusMessage(const char *fmt, ...){
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}


/* INPUT */
//passando um ponteiro de função como parametro
char *editorPrompt(char *prompt, void (*callback)(char *, int)){
	//input do usuário
	size_t bufsize = 128;
	char *buf = malloc(bufsize);
	
	size_t buflen = 0;
	buf[0] = '\0';
	
	while(1){
		editorSetStatusMessage(prompt, buf);
		editorRefreshScreen();

		int c = editorReadKey();
		//deleta um caractere no promp
		if(c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE){
			if(buflen != 0) buf[--buflen] = '\0';
		}else if(c == '\x1b'){
			//verifica se o usuario pressionou esc, e com isso sai do prompt
			editorSetStatusMessage("");
			if (callback) callback(buf,c);
			free(buf);
			return NULL;
		} else if(c == '\r'){
			if(buflen != 0){
				editorSetStatusMessage("");
				if (callback) callback(buf,c);
				return buf;
			}
			//verifica se o c é um caracter de comando, se não for executamos o resto
		} else if (!iscntrl(c) && c < 128){
			//verica se o buflen chegou na sua capacidade maxima
			if(buflen == bufsize - 1){
				//dobramos o tamanho do buffer
				bufsize *= 2;
				buf = realloc(buf, bufsize);
			}
			
			buf[buflen++] = c;
			buf[buflen] = '\0';
		}


		if (callback) callback(buf,c);
	}
	
}


void editorMoveCursor(int key){
	//faz com que o cursor não passe da linha 
	erow *row = (E.cursorY >= E.numrows) ? NULL : &E.row[E.cursorY];


	switch(key){
		case MOTIONKEY_H:
		case ARROW_LEFT:
			if(E.cursorX != 0){
				E.cursorX--;
			}else if(E.cursorY > 0){
				//faz com que se você apertar para esquerda no começo da linha o cursor vai para linha anterior. (vou tirar isso dps XP)
				E.cursorY--;
				E.cursorX = E.row[E.cursorY].size;
			}
			break;
		case MOTIONKEY_L:
		case ARROW_RIGHT:
			//verifica se o cursor está num local seja maior que a linha
			if(row && E.cursorX < row->size){
				E.cursorX++;
			}else if(row && E.cursorX == row->size){
				//faz com que se você apertar para direita no final da linha o cursor vai para a proxima linha.
				//
				E.cursorY++;
				E.cursorX = 0;
			}
			break;
		case MOTIONKEY_K:
		case ARROW_UP:
			if(E.cursorY != 0){
				E.cursorY--;
			}
			break;
		case MOTIONKEY_J:
		case ARROW_DOWN:
			if(E.cursorY != E.numrows) {
				E.cursorY++;
			}
		break;
	}
	//corrige caso o cursor esteja em uma posição em uma linha e na outra essa posição seja inacessível, fazendo com que ele pule para o final da proxima linha
	row = (E.cursorY >= E.numrows) ? NULL : &E.row[E.cursorY];
	int rowlen = row ? row->size : 0;
	if(E.cursorX > rowlen){
		E.cursorX = rowlen;
	}

}

void editorProcessKeypress(){
	//variáveis estáticas escapam do escopo da função e duram o programa todo
	static int quit_times = KILO_QUIT_TIMES;

	int c = editorReadKey();
	//MODO INSERT
	if(mode == 1){
		char *displayMode = (mode) ? "[--INSERT--]" : "[--NORMAL--]";
		editorSetStatusMessage(displayMode);
		switch(c) {
			//tecla enter
			case '\r':
				editorInsertNewLine();
				break;


			case CTRL_KEY('q'):
				if(E.dirty && quit_times > 0){
					editorSetStatusMessage("WARNING!!! File has unsaved changes. "
							"Press Ctrl-Q %d more times to quit", quit_times);
					quit_times--;
					return;
				}
				write(STDOUT_FILENO, "\x1b[2J", 4);
				write(STDOUT_FILENO, "\x1b[H", 3);
				exit(0);
				break;

			case CTRL_KEY('s'):
				editorSave();
				break;

			case HOME_KEY:
				E.cursorX = 0;
				break;

			case END_KEY:
				if(E.cursorY < E.numrows)
					E.cursorX = E.row[E.cursorY].size;
				break;


			case CTRL_KEY('f'):
				editorFind();
			break;
			case BACKSPACE:
			case CTRL_KEY('h'):
			case DEL_KEY:
				if(c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
				editorDeleteChar();
				break;
			
			case PAGE_UP:
			case PAGE_DOWN:
				{
					if(c == PAGE_UP) {
						E.cursorY = E.rowoff;
					} else if(c == PAGE_DOWN) {
						E.cursorY = E.rowoff + E.screenrows - 1;
						if(E.cursorY > E.numrows) E.cursorY = E.numrows;
					}
					int times = E.screenrows;
					while(times--)
						editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
				}
				break;

			case ARROW_UP:
			case ARROW_DOWN:
			case ARROW_LEFT:
			case ARROW_RIGHT:
				editorMoveCursor(c);
				break;

			case CTRL_KEY('l'):
			case '\x1b':
				mode = 0;
				break;

			default:
				editorInsertChar(c);
				break;
		}
	//MODO NORMAL	
	}else{
		char *displayMode = (mode) ? "[--INSERT--]" : "[--NORMAL--]";
		editorSetStatusMessage(displayMode);
		switch(c){
			//tecla enter
			case '\r':
				editorInsertNewLine();
				break;


			case CTRL_KEY('q'):
				if(E.dirty && quit_times > 0){
					editorSetStatusMessage("WARNING!!! File has unsaved changes. "
							"Press Ctrl-Q %d more times to quit", quit_times);
					quit_times--;
					return;
				}
				write(STDOUT_FILENO, "\x1b[2J", 4);
				write(STDOUT_FILENO, "\x1b[H", 3);
				exit(0);
				break;

			case CTRL_KEY('s'):
				editorSave();
				break;

			case HOME_KEY:
				E.cursorX = 0;
				break;

			case END_KEY:
				if(E.cursorY < E.numrows)
					E.cursorX = E.row[E.cursorY].size;
				break;


			case CTRL_KEY('f'):
				editorFind();
			break;

			case PAGE_UP:
			case PAGE_DOWN:
				{
					if(c == PAGE_UP) {
						E.cursorY = E.rowoff;
					} else if(c == PAGE_DOWN) {
						E.cursorY = E.rowoff + E.screenrows - 1;
						if(E.cursorY > E.numrows) E.cursorY = E.numrows;
					}
					int times = E.screenrows;
					while(times--)
						editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
				}
				break;

			case ARROW_UP:
			case ARROW_DOWN:
			case ARROW_LEFT:
			case ARROW_RIGHT:
				editorMoveCursor(c);
				break;

			case MOTIONKEY_K:
			case MOTIONKEY_J:
			case MOTIONKEY_H:
			case MOTIONKEY_L:
				editorMoveCursor(c);
				break;


			case CTRL_KEY('l'):
			case '\x1b':
				break;
		}	
	}
	if(c == 105) mode = 1;
	quit_times = KILO_QUIT_TIMES;
}

/* INIT */
void initEditor(){ 
	E.cursorX = 0; 
	E.cursorY = 0;
	E.renderX = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.rowoff = 0;
	E.row = NULL;
	E.filename = NULL;
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;
	E.dirty = 0;

	if(getWindowSize(&E.screenrows, &E.screencols) == -1)
		die("getWindowSize");

	E.screenrows -= 2;
}


int main(int argc, char *argv[]){
	enableRawMode();
	initEditor();
	if(argc >= 2){
		editorOpen(argv[1]);
	}
	char *displayMode = (mode) ? "[--INSERT--]" : "[--NORMAL--]";
	editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = search | %s", displayMode);

	while(1){
		editorRefreshScreen();
		editorProcessKeypress();
	}
	

	return 0;
}
