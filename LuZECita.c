#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include <time.h>
#include <linux/unistd.h>
#include <errno.h>

//Numero maximo de clientes que se pueden atender a la vez
#define MAX_CLIENTES 20

//Numero de tecnicos, responsables de reparaciones y encargados
#define NUM_TECNICOS 2
#define NUM_RESPONSABLES 2
#define NUM_ENCARGADOS 1

/*Mensajes de log*/
char *entrada = "Hora de entrada al sistema.";
char *appDificil = "El cliente se ha ido porque encuentra la aplicación difícil.";
char *muchaEspera = "El cliente se ha ido porque se ha cansado de esperar.";
char *noInternet = "El cliente se ha ido porque ha perdido la conexión a internet.";
char *esperaAtencion = "El cliente espera por la atención domiciliaria";
char *atencionFinaliza = "La atención domiciliaria ha finalizado";
char *clienteAtendido = "El cliente ha sido atendido correctamente y se va.";

/*Variables globales*/
pthread_mutex_t semaforoFichero;
pthread_mutex_t semaforoColaClientes;
pthread_mutex_t semaforoSolicitudes;

//Array de hilo-cliente
pthread_t *arrayHilosClientes;

int nClientesApp; //Contador clientes app
int nClientesRed; //Contador clientes red
int nClientes;	//Número total de clientes
int nSolicitudesDomiciliarias; //N solicitudes domiciliarias (4 para que el responsable viaje)

/*Arrays dinámicos que almacenan a los clientes (estructura)*/
struct cliente *arrayClientes;
struct cliente *arrayClientesSolicitudes; //Clientes a los que tiene que visitar el responsable

/*Estructuras*/
//estructura que guarda la informacion del cliente
struct cliente{
	char *id;		//no seria mejor poner el id como int? NO PORQUE EL IDENTIFICADOR ES CLIAPP_1
	int atendido;	//0 no atendido, 1 está siendo atendido, 2 ha sido atendido
	char *tipo;		//app o red
	int prioridad;	//Array de hilo-cliente
	int solicitud;	//0 no solicitud, 1 solicita atención domiciliaria
};

//estructura que guarda la informacion del trabajador
struct trabajador{ 
	char *id;	
	int clientesAtendidos;
	char *tipo; //Tecnico, responsable o encargado T, R o E
	int libre; //0 si esta libre 1 si esta ocupado
};

/*Fichero de log*/
FILE *logFile;

/*Crea un nuevo cliente cuando recibe una de las dos señales definidas para ello*/
void crearNuevoCliente(int signum){

	pthread_mutex_lock(&semaforoColaClientes);		//el hilo adquiere el bloqueo del mutex y puede acceder a la cola de clientes

	if(nClientes<MAX_CLIENTES){
		struct cliente nuevoCliente;		//creamos la variable nuevoCliente para guarda informacion en el struct
		nClientes++;	//aumentamos el contador de clientes totales

		char numeroId[2];		//creamos numeroId
		printf("Hay un nuevo cliente\n");
		
		//Vemos si el cliente es de la app o red
		switch(signum){
			case SIGUSR1:
				if(signal(SIGUSR1, crearNuevoCliente)==SIG_ERR){
					perror("Error en signal");
					exit(-1);
				}
			
				sprintf(numeroId, "%d", nClientesApp);
				nuevoCliente.id=strcat("cliapp_", numeroId);
				nuevoCliente.atendido=0;	//0 indica que el cliente todavia no ha sido atendido
				nuevoCliente.tipo="App";	//cliente de la app
				nClientesApp++;				//aumentamos el contador de clientes de app
				nuevoCliente.solicitud=0;	//ponemos la solicitud del cliente en 0
				nuevoCliente.prioridad=calculaAleatorios(1, 10);	//damos un numero de prioridad aleatorio al cliente
				printf("El cliente es %c\n", nuevoCliente.id);
				
			break;

			case SIGUSR2:
				if(signal(SIGUSR2, crearNuevoCliente)==SIG_ERR){
					perror("Error en signal");
					exit(-1);
				}

				sprintf(numeroId, "%d", nClientesRed);
				nuevoCliente.id=strcat("clired_", numeroId);
				nuevoCliente.atendido=0;
				nuevoCliente.tipo="Red";	//cliente de red
				nClientesRed++;		//aumentamos el contador de clientes de red
				nuevoCliente.prioridad=calculaAleatorios(1, 10);
				printf("El cliente es %c\n", nuevoCliente.id);

			break;

		}

		arrayClientes[nClientes-1]=nuevoCliente;		//asigna la estructura nuevoCliente al ultimo elemento de arrayClientes

		pthread_t hiloClientes;		//hiloClientes es una variable de tipo pthread_t que se ha declarado para almacenar el identificador de un hilo específico
		arrayHilosClientes[nClientes-1]=hiloClientes;		//asigna la estructura hiloClientes al ultimo elemento de arrayHilosClientes

		pthread_create(&arrayHilosClientes[nClientes], NULL, accionesCliente, (void*)(nClientes-1));		//pthread_create() esta creando un nuevo hilo y asignandole la funcion accionesCliente() como funcion de entrada. El hilo se almacena en el elemento nClientes de arrayHilosClientes. La funcion accionesCliente() recibe como argumento el indice del elemento del arreglo de clientes correspondiente al hilo

		pthread_mutex_unlock(&semaforoColaClientes);		//libera el hilo del mutex y permite a otros otros hilos poder entrar en la cola de cliente y asi en la seccion critica
	}else{
		pthread_mutex_unlock(&semaforoColaClientes);
		printf("No se ha podido atender al cliente\n");

		return;
	}
}

void accionesCliente (void* nuevoCliente) {

	(int *) nuevoCliente;
	/*	
	Hay que cambiar los nuevoCliente.tipo, etc. por arrayClientes[(nuevoCliente)].atendido, etc.
	*/

	/*Guardar en log la hora de entrada al sistema y tipo de cliente*/
	pthread_mutex_lock(&semaforoFichero);
	writeLogMessage(nuevoCliente.id, entrada); //Escribe el id del cliente y el char "entrada" (variable global)
	writeLogMessage(nuevoCliente.id, nuevoCliente.tipo); //Escribe el id y tipo del cliente
	pthread_mutex_unlock(&semaforoFichero);

	/*Comprobar si el cliente está atendido - Comprobar que no lo está*/
	pthread_mutex_lock(&semaforoColaClientes);

	do {
		int comportamiento = calculaAleatorios(1, 100);

		if (comportamiento <= 10) { //Un 10% encuentra la app dificil y se va
			clienteFuera(nuevoCliente.id, appDificil); //Se escribe en el log

			//Liberar espacio en la cola de clientes
			liberaEspacioClientes(nuevoCliente.tipo); //Funcíón que resta un cliente de nClientes y nClientes(tipo) cuando este se va

			//Libera un hueco en el array de clientes
			borrarCliente((int )nuevoCliente); //Función que borra al cliente del arrayClientes
				
			//Libera el mutex y finaliza el hilo
			pthread_mutex_unlock(&semaforoColaClientes); //Se libera el mutex antes de finalizar el hilo
			pthread_exit(NULL); //Se finaliza el hilo cliente
		}
		else if (comportamiento > 10 && comportamiento <= 30) { //Un 20% se cansa de esperar
			sleep(8);

			clienteFuera(nuevoCliente.id, muchaEspera);

			//Liberar espacio en la cola de clientes
			liberaEspacioClientes(nuevoCliente.tipo); //Funcíón que resta un cliente de nClientes y nClientes(tipo) cuando este se va

			//Libera un hueco en el array de clientes
			borrarCliente((int )nuevoCliente); //Función que borra al cliente del arrayClientes

			//Libera el mutex y finaliza el hilo
			pthread_mutex_unlock(&semaforoColaClientes);
			pthread_exit(NULL);
		}
		else if (comportamiento > 30 && comportamiento <= 35) { //Un 5% del restante pierde la conexión a internet
			clienteFuera(nuevoCliente.id, noInternet);

			//Liberar espacio en la cola de clientes (Antes o después dek unlock & exit??)
			liberaEspacioClientes(nuevoCliente.tipo);

			//Libera un hueco en el array de clientes
			borrarCliente((int )nuevoCliente); //Función que borra al cliente del arrayClientes

			//Libera el mutex y finaliza el hilo
			pthread_mutex_unlock(&semaforoColaClientes);
			pthread_exit(NULL);
		}
		else {//Si se queda duerme 2 segundos y vuelve a comprobar si se va o se queda
			sleep(2);
		}

	} while (nuevoCliente.atendido == 0);

	/*Cliente siendo atendido, comprueba cada 2 segundos que la atención haya terminado*/
	while (arrayClientes[(nuevoCliente)].atendido == 1){
		sleep(2);
	}

	/**************************************************************************************************/

	/*Cliente de tipo red y solicita atención domiciliaria una vez <<ha sido atendido>> */
	if (arrayClientes[(nuevoCliente)].tipo == "Red" /*&& arrayClientes[(nuevoCliente)].atendido == 2*/ && arrayClientes[(nuevoCliente)].solicitud == 1) {
		 
  	     //pthread_mutex_lock(&semaforoSolicitudes) con variables condición, ahora mismo no lo hago
  	     
  	    do {
			if (nSolicitudesDomiciliarias < 4) {

				nSolicitudesDomiciliarias++; //Se incrementan las solicitutes si hasta ahora eran < 4
  	        	 
  	        	//Se escribe en el log que el cliente quiere atención
  	        	pthread_mutex_lock(&semaforoFichero);
  	        	writeLogMessage(nuevoCliente.id, esperaAtencion);
  	        	pthread_mutex_unlock(&semaforoFichero);
  	        	
  	        	//Se cambia el valor de solicitud a 1
				//////////////////////No definitivo
  	        	pthread_mutex_lock(&semaforoColaClientes);
  	        	nuevoCliente.solicitud = 1;
  	        	pthread_mutex_unlock(&semaforoColaClientes);
				////////////////////////////////
  	        	
  	        	/*
  	        	5.III y 5.IV (No tengo ganas de hacerlo ahora)
  	        	*/
  	        	
  	        	//pthread_mutex_unlock(&semaforoSolicitudes) con variables condición, ahora mismo no lo hago
  	        	
  	        	//Se escribe en el log que la atencion ha finalizado
  	        	pthread_mutex_lock(&semaforoFichero);
  	        	writeLogMessage(nuevoCliente.id, atencionFinaliza);
  	        	pthread_mutex_unlock(&semaforoFichero);
			} 
			else {
  	        	sleep(3);
			}
  	     
		} while (nSolicitudesDomiciliarias < 4);
  	}

	/**************************************************************************************************/
	
	/*Cliente atendido satisfactoriamente*/
	//Liberar espacio en la cola de clientes (Antes o después dek unlock & exit??)
	liberaEspacioClientes(arrayClientes[(nuevoCliente)].tipo);
	
	//Libera un hueco en el array de clientes
	//No estoy seguro de si es esto, arrayClientes o nuevoCliente simplemente
	borrarCliente((int )nuevoCliente); //Función que borra al cliente del arrayClientes
	
	//Libera el mutex y finaliza el hilo
	pthread_mutex_unlock(&semaforoColaClientes);

	//Escribe en el log que el cliente ha sido atendido y se va
  	pthread_mutex_lock(&semaforoFichero);
  	writeLogMessage(arrayClientes[(nuevoCliente)].id, clienteAtendido);
  	pthread_mutex_unlock(&semaforoFichero);

	//Fin del hilo cliente
	pthread_exit(NULL);
	
} 

/*Función que escribe en el log cuando un cliente se va antes de ser atendido*/
void clienteFuera(char *id, char *razon) {
	pthread_mutex_lock(&semaforoFichero);
	writeLogMessage(id, razon); //Escribe el id del cliente y el char "appDificil" 
	pthread_mutex_unlock(&semaforoFichero);
}

/*Función que resta 1 a los clientes totales y a cada tipo cada vez que es atendido o se va sin serlo*/
void liberaEspacioClientes(char *tipoCliente) {
	if (nuevoCliente.tipo == "App") {
		nClientesApp--;	
	} else {
		nClientesRed--;
	}
	nClientes--;
}

/*Función que elimina un cliente del array de clientes*/
void borrarCliente (int posicionCliente) {

	for (int i = posicionCliente; i < (nClientes - 1); i++) {
		arrayClientes[i] = arrayCliente[i + 1]; //El cliente en cuestión se iguala al de la siguiente posición (inicilizado todo a 0)
	}

	struct cliente structVacia; //Se declara un cliente vacío
	arrayClientes[nClientes - 1] = structVacia; //El cliente pasa a estar vacío (todo a 0)
}

//¿Esto es la función "accionesTecnico" que ejecutan los hilos de tecnicos?  
/*funcion del técnico (CUIDADO) es decir atender a los clientes con problemas en la app*/
void atenderClienteApp(){
	//IMPORTANTE:::::AÑADIR LO DE LA PRIORIDAD
	//Se comprueba si el tenico esta libre
	struct cliente cliente;
	struct trabajador trabajador;

	if(tecnico1.libre==0){
		printf("Se va a atender a un cliente con problemas en la app\n");
		printf("Bienvenido cliente, le esta atendiendo el tecnico 1\n");
		tecnico1.libre=1;
		//Compruebo que el cliente que atiendo sea de app
		if(cliente.tipo==App){
			printf("El cliente ha sido atendido\n");
			//Cambio su estado a atendido
			cliente.atendido=1;
			++tecnico1.clientesAtendidos;
			//Si hay 5 descanso 5 seg
			if(tecnico1.clientesAtendidos==5){
				tecnico1.libre=1;
				sleep(5);
			}
		}
	}else if (tecnico2.libre==0){
		printf("Se va a atender a un cliente con problemas en la app\n");
		printf("Bienvenido cliente, le esta atendiendo el tecnico 2\n");
		tecnico2.libre=1;
		//Compruebo que el cliente que atiendo sea de app
		if(cliente.tipo==App){
			printf("El cliente ha sido atendido\n");
			//Cambio su estado a atendido
			cliente.atendido=1;
			++tecnico2.clientesAtendidos;
			//Si hay 5 descanso 5 seg
			if(tecnico2.clientesAtendidos==5){
				tecnico2.libre=1;
				sleep(5);
			}
		}
	}else{
		printf("Ningun tecnico esta libre en este momento\n");
	}
}

/*************************************************************************************************************************************************/

/*Función que calcula números aleatorios*/
int calculaAleatorios(int min, int max) {
	srand(time(NULL));
	
	return rand() % (max - min + 1) + min;
}

/*Función que escribe los mensajes en log*/
void writeLogMessage(char *id, char *msg) {
	//Se calcula la hora actual
	time_t now = time(0);
	struct tm *tlocal = localtime(&now);
	char stnow[25];
	strftime(stnow, 25, "%d/%m/%y  %H:%M:%S", tlocal);

	//Escribimos en el log
	logFile = fopen("registroTiempos.log", "a");
	fprintf(logFile, "[%s] %s: %s\n", stnow, id, msg);
	fclose(logFile);
}

//IMPORTANTE::::::AÑADIR CONDICION DE QUE SI ESTAN LOS DOS OCUPADOS ATENDERLOS ENCARGADO

//void atender_cliente(){
//	printf("Se esta atendiendo al cliente %d (%c)\n, c.id, c.tipo");
//	sleep(1);	//se simula el tiempo de atencion
//}

/*Función que finaliza el programa al recibir la señal*/
void finalizarPrograma (int signal) {

	//Antes de que finalice se debe terminar de atender a todos los clientes en cola

	/*Puede que los pthread_join vayan aquí? Que espere a terminar la ejecución antes de finalizar el programa*/

	printf("\nAdiós!\n");

	exit(0);
}

int main(int argc, char* argv[]) {
	
	/* DECLARACIONES DE RECURSOS */
	pthread_t tecnico1, tecnico2, responsable1, responsable2, encargado, atencionDomiciliaria;

	arrayHilosClientes = (pthread_t *) malloc (nClientes * sizeOf(struct cliente *)); //Array dinámico de hilos de clientes
	arrayClientes = (struct cliente *) malloc (nClientes * sizeOf(struct cliente *)); //array del total de clientes
	arrayClientesSolicitudes = (struct cliente *) malloc (4 * sizeOf(struct cliente *)); // array de clientes con solicitudes (4 para que salga el responsable)
	
	/*Mostramos por pantalla el PID del programa para enviar las señales*/
	printf("\nPID: %s\n", getpid());

	/* SEÑALES */
	//Cliente App
	if (signal(SIGUSR1, crearNuevoCliente) == SIG_ERR) {
		perror("Error en la señal");

		exit(-1);
	}

	//Cliente Red
	if (signal(SIGUSR2, crearNuevoCliente) == SIG_ERR) {
		perror("Error en la señal");

		exit(-1);
	}
	
	//Finalización del programa 
	if (signal(SIGINT, finalizarPrograma) == SIG_ERR) {
		perror("Error en la señal");

		exit(-1);
	}

	/* INICIALIZACIÓN DE RECURSOS */
	//Inicialización de los semáforos
	if (pthread_mutex_init(&semaforoFichero, NULL) != 0) exit(-1);
	if (pthread_mutex_init(&semaforoColaClientes, NULL) != 0) exit(-1);
	if (pthread_mutex_init(&semaforoSolicitudes, NULL) != 0) exit(-1);

	//Contadores de clientes
	int nClientes = 0;
	int nClientesApp = 0;
	int nClientesRed = 0;

	//Lista clientes
	struct cliente *clienteApp;
		clienteApp -> id = (char *) malloc (12 * sizeof(char));
		clienteApp -> id = "cliapp_"; //Luego en cada cliente creado se le añade el número al final de la cadena de caracteres con ¿strcat? creo
		
		clienteApp -> atendido = 0;

		clienteApp -> tipo = (char *) malloc (12 * sizeof(char));
		clienteApp -> tipo = "Aplicacion";

		clienteApp -> prioridad = 0;
		clienteApp -> solicitud = 0;

	struct cliente *clienteRed ;
		clienteRed  -> id = (char *) malloc (12 * sizeof(char));
		clienteRed  -> id = "clired_"; //Luego en cada cliente creado se le añade el número al final de la cadena de caracteres con ¿strcat? creo
		
		clienteRed  -> atendido = 0;

		clienteRed  -> tipo = (char *) malloc (18 * sizeof(char));
		clienteRed  -> tipo = "Problema en red";

		clienteRed  -> prioridad = 0;
		clienteRed -> solicitud = 0;

	//Lista trabajadores
	/*Técnicos*/
	struct trabajador *tecnico_1;
	tecnico_1 -> id = (char *) malloc (10  * sizeof(char));
	tecnico_1 -> id = "tecnico_1";

	tecnico_1 -> clientesAtendidos = 0;	//No estoy seguro si hace falta inicializar a 0 esto o ya lo está
	
	tecnico_1 -> tipo = (char *) malloc (8 * sizeof(char));
	tecnico_1 -> tipo = "Tecnico";

	tecnico_1 -> libre = 0;	//No estoy seguro si hace falta inicializar a 0 esto o ya lo está

	struct trabajador *tecnico_2;
	tecnico_2 -> id = (char *) malloc (10  * sizeof(char));
	tecnico_2 -> id = "tecnico_2";

	tecnico_2 -> clientesAtendidos = 0;	//No estoy seguro si hace falta inicializar a 0 esto o ya lo está
	
	tecnico_2 -> tipo = (char *) malloc (8 * sizeof(char));
	tecnico_2 -> tipo = "Tecnico";

	tecnico_2 -> libre = 0;	//No estoy seguro si hace falta inicializar a 0 esto o ya lo está

	/*Responsables de reparaciones*/
	struct trabajador *responsable_1;
	responsable_1 -> id = (char *) malloc (14  * sizeof(char));
	responsable_1 -> id = "responsable_1";

	responsable_1 -> clientesAtendidos = 0;	//No estoy seguro si hace falta inicializar a 0 esto o ya lo está
	
	responsable_1 -> tipo = (char *) malloc (20 * sizeof(char));
	responsable_1 -> tipo = "Resp. Reparaciones";

	responsable_1 -> libre = 0;	//No estoy seguro si hace falta inicializar a 0 esto o ya lo está

	struct trabajador *responsable_2;
	responsable_2 -> id = (char *) malloc (14  * sizeof(char));
	responsable_2 -> id = "responsable_2";

	responsable_2 -> clientesAtendidos = 0;	//No estoy seguro si hace falta inicializar a 0 esto o ya lo está
	
	responsable_2 -> tipo = (char *) malloc (20 * sizeof(char));
	responsable_2 -> tipo = "Resp. Reparaciones";

	responsable_2 -> libre = 0;	//No estoy seguro si hace falta inicializar a 0 esto o ya lo está

	/*Encargado*/
	struct trabajador *encargado_;
	encargado_ -> id = (char *) malloc (10 * sizeof(char));
	encargado_ -> id = "encargado";

	encargado_ -> clientesAtendidos = 0;	//No estoy seguro si hace falta inicializar a 0 esto o ya lo está

	encargado_ -> tipo = (char *) malloc (10 * sizeof(char));
	encargado_ -> tipo = "Encargado";

	encargado_ -> libre = 0;	//No estoy seguro si hace falta inicializar a 0 esto o ya lo está

	//Fichero de log
	logFile = fopen("registroTiempos.log", "wt"); //Opcion "wt" = Cada vez que se ejecuta el programa, si el archivo ya existe se elimina su contenido para empezar de cero
	fclose(logFile);

	//Variables relativas a la solicitud de atención domiciliaria
	int nSolicitudesDomiciliarias = 0; //Contador para las solicitudes, cuando sea 4 se envía atención domiciliaria

	//Variables condicion
	//*** MUY IMPORTANTE *** -> Esto creo que es un array condicion similar al de la práctica 8

	/* CREACIÓN DE HILOS DE TECNICOS, RESPONSABLES, ENCARGADO Y ATENCION DOMICILIARIA */
	//Se pasa como argumento la estructura del trabajador que ejecuta el hilo
	pthread_create(&tecnico1, NULL, accionesTecnico, (void *) tecnico_1);
	pthread_create(&tecnico2, NULL, accionesTecnico, (void *) tecnico_2);
	pthread_create(&responsable1, NULL, accionesTecnico, (void *) responsable_1);
	pthread_create(&responsable2, NULL, accionesTecnico, (void *) responsable_2);
	pthread_create(&encargado, NULL, accionesEncargado, (void *) encargado_);
	pthread_create(&atencionDomiciliaria, NULL, accionesTecnicoDomiciliario, (void *)/*argumentos necesarios para la ejecución del hilo, ningún trabajador en particular*/);

	/* ESPERAR POR SEÑALES INFINITAMENTE */
	while(1) {
		pause();
	}

	/*No estoy seguro de si hacen falta, probar con ellos y sin ellos cuando se ejecute*/
	//pthread_join(tecnico1, NULL);
	//pthread_join(tecnico2, NULL);
	//pthread_join(responsable1, NULL);
	//pthread_join(responsable2, NULL);
	//pthread_join(encargado, NULL);
	//pthread_join(atencionDomiciliaria, NULL);

	//pthread_mutex_destroy(); no creo que sea necesario pero queda aquí por si acaso da un problema de memoria o algo

	/* LIBERACIÓN DE MEMORIA */
	free(arrayHilosClientes);
	free(arrayClientes);
	free(arrayClientesSolicitudes);

	return 0;
}
