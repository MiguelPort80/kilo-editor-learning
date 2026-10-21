#include <unistd.h>

int main(){
	char c;
	/*	queremos que a função read leia o byte 1 do standard input na variavel c e continue lendo até não ter mais bytes para ler
	 *	read() retorna o número de bytes que ela leu e retorna 0 quando chega no final de um arquivo (EOF)
	 *	por padrão o terminal abre no canonical mode, que faz com que o que input do seu teclado apenas seja enviado após você apertar enter
	 *	então podemos ficar digitando infinitamente e corrigir nossos erros usando backspace, mas para um editor de texto isso não é bom
	 *	O que precisamos é do Raw Mode
	*/

	while(read(STDIN_FILENO, &c, 1) == 1 && c != 'q');
	return 0;
}
