#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <math.h>
#include <time.h>


//#define TIMEOUT 0.25	    //secondes
#define TIMEOUT_FACTOR 1.4
#define ALPHA 0.9
#define SRTT_INIT	0.5		//secondes
#define DATA_SIZE 1494 		//bytes
#define TAILLE_ENVOI 1500 	//bytes
#define MAXSEG 1024         //segments

int checkSYN(char str[64]) {
	
	char begin[3] = "SYN";
	int i;
	for(i = 0; i < 3; i++) {
		if(begin[i] != str[i]) return 0;
	}
	return 1;
}

int checkACK(char str[64]) {
	
	char begin[3] = "ACK";
	int i;
	for(i = 0; i < 3; i++) {
		if(begin[i] != str[i]) return 0;
	}
	return 1;
}



int main (int argc, char* argv []){

	printf("* * * SERVEUR * * *\n\n");
	
	int port_controle	= atoi(argv[1]);
	int port_data 		= 1025;
	
	int socket_controle = socket(AF_INET, SOCK_DGRAM, 0);
	if(socket_controle == -1){
		printf("Erreur de creation socket contrôle\n");
		return -1;
	}

	printf("Port controle : %d\n", port_controle);
	printf("Socket contrôle : %d\n", socket_controle);
	
	
	int reutilise = 1;
	setsockopt(socket_controle, SOL_SOCKET, SO_REUSEADDR, &reutilise, sizeof(reutilise));
	
	struct sockaddr_in adresse_client_controle;
	struct sockaddr_in adresse_client_data;
	
	memset((char*)&adresse_client_controle, 0, sizeof(adresse_client_controle));
	memset((char*)&adresse_client_data, 	0, sizeof(adresse_client_data));
	
	adresse_client_controle.sin_family		= AF_INET;
	adresse_client_controle.sin_port		= htons(port_controle);
	adresse_client_controle.sin_addr.s_addr = htonl(INADDR_ANY);

	adresse_client_data.sin_family			= AF_INET;
	adresse_client_data.sin_addr.s_addr 	= htonl(INADDR_ANY);
	
	if( bind(socket_controle,  (struct sockaddr *)&adresse_client_controle, sizeof(adresse_client_controle)) == -1)
	{
		printf("Erreur de creation bind controle\n");
		close(socket_controle); 
		return -2;
	}
	
	// Déclarations
	socklen_t taille_adresse_controle = sizeof(adresse_client_controle);
	char buffer[2048];
	char fileName[1000];
	char SYN_ACK[64];
	int socket_data;
	int run = 1;
	int numAck, timeoutReached;
	int ackMaxRecu = 0;
	int i, j;
	int ackRecu = 0;
	int nbSeg = 1;
	int lastSegReached = 0;
	clock_t start;	//Début du timer pour le RTT
	clock_t end;	//Fin du timer pour le RTT
	double srtt = SRTT_INIT;	//Estimation du RTT
	double rtt;		//Temps "aller-retour"
	
	while(run)
	{	
		//Préparation de l'adresse client pour les data en vue d'une prochaine connexion
		//Nouvelle socket
		socket_data = socket(AF_INET, SOCK_DGRAM, 0);
		if (socket_data == -1)
		{
			printf("Erreur de creation socket data\n");
			return -1;
		}
		setsockopt(socket_data, SOL_SOCKET, SO_REUSEADDR, &reutilise, sizeof(reutilise));

		//Nouveau port
		adresse_client_data.sin_port = htons(port_data);
		socklen_t taille_adresse_data = sizeof(adresse_client_data);
		if( bind(socket_data,  (struct sockaddr *)&adresse_client_data, taille_adresse_data) == -1)
		{
			printf("Erreur de creation bind data\n");
			close(socket_data); 
			return -2;
		}

		printf("Port data : %d\n", port_data);
		printf("Socket data : %d\n", socket_data);

		/********** PHASE DE CONNEXION **********/

		sprintf(SYN_ACK, "SYN-ACK%d", port_data);
		//Attente d'un segment depuis le client contenant SYN:
		while( !checkSYN(buffer) )
		{
			if(  recvfrom(socket_controle, &buffer, sizeof(buffer), 0, (struct sockaddr *)&adresse_client_controle, &taille_adresse_controle) == -1)
			{
				printf("\nErreur réception SYN \n");
				close(socket_controle);
				return -3;
			}
			else
			{
				printf("\nMessage reçu: %s \n",buffer);
			}
		}
		
		//Envoi vers le client d'un message SYN-ACK[port_data]:
		if ( sendto( socket_controle, &SYN_ACK, sizeof(SYN_ACK), 0, (struct sockaddr *)&adresse_client_controle, taille_adresse_controle ) == -1 )
		{
			printf("\nErreur envoie SYN+ACK \n");
			close(socket_data);
			return -4;
		}
		printf("Message SYN-ACK%d envoyé au client\n", port_data);
		
		//Attente d'un segment depuis le client contenant ACK:
		while( !checkACK(buffer) )
		{
						
			if(  recvfrom(socket_controle, &buffer, sizeof(buffer), 0, (struct sockaddr *)&adresse_client_controle, &taille_adresse_controle) == -1)
			{
				printf("\nErreur réception ACK \n");
				close(socket_data);
				return -3;
			}
		}
		printf("Message ACK Recu \n\n");

		/********** FIN DE LA CONNEXION **********/
		


		int pid = fork();
			
		if(pid > 0)
		{
			// Prossesus Pere
			port_data++;
			sleep(0.5);
			close(socket_data);
			printf("Socket data fermée \n\n");
		}
		else if (pid == 0)
		{
			// Processus Fils
			
			/** FIN PROTOCOLE DEBUT DE CONNEXION **/

			//Fermeture de la socket de connexion
			close(socket_controle);

			
			int numSeq = 1;
		
			int nameOfFileReceived = 0;
			
			while(nameOfFileReceived == 0)
			{
			
				//Reception du nom du fichier recherché par le client
				if(recvfrom(socket_data, fileName, 64, 0, (struct sockaddr *)&adresse_client_data, &taille_adresse_data) == -1)
				{
					printf("Erreur reception message \n");
					close(socket_data);
					return -3;
				}
				else printf("\nLe serveur a recu :\n%s\n\n",fileName);

				nameOfFileReceived++;

			}
			
			//Ouverture du fichier demande par le client
			FILE* fp;
			fp = fopen(fileName, "rb");
			
			if(fp == NULL)
			{
				printf("file does not exist\n");
			}

			fseek(fp, 0, SEEK_END);

			char toSend[MAXSEG][TAILLE_ENVOI];
			printf("%ld\n", sizeof(buffer));
			char read_buffer[DATA_SIZE];
			memset(read_buffer, 0, sizeof(read_buffer));
			int read_bytes;
			int dernierAckRecu = 0;
			int nombre_de_fois_duplique = 0;

			fseek(fp, 0, SEEK_SET);
			do
			{
				if (ackRecu > dernierAckRecu)
				{
					dernierAckRecu = ackRecu;
				}
				fseek(fp, (numSeq - 1) * DATA_SIZE, SEEK_SET);
				
				//===== Boucle de chargement des segments et de leur envoi =====
				
				for(i = 0; i < nbSeg; i++) {
					if(!feof(fp)) {
						read_bytes = fread(read_buffer, 1, DATA_SIZE, fp);
						if(read_bytes < 0) {
							printf("Echec de la copie des données du fichier dans le buffer\n");
							exit(-1);
						}
						
						sprintf(toSend[i], "%06d", numSeq+i);
						memmove(toSend[i]+6, read_buffer, DATA_SIZE);
					}
					else {
						lastSegReached = 1;
						printf("EOF atteint.\nDernier segment à envoyer : %d.\n", numSeq + (i-1));
						j = i;
						break;
					}
					
				}
				printf("Les segments %d à %d vont être envoyés\n\n", numSeq, numSeq+nbSeg-1);
				
				//***** Envoi des segments chargés *****//
				int sent_bytes;
				if (lastSegReached) nbSeg = j;
				for(i = 0; i < nbSeg; i++) {
					if(i == nbSeg -1) { // On ne veut pas que tous les segments de la dernière vague d'envoi fassent la taille du dernier segment lu */
						sent_bytes = sendto(socket_data, toSend[i], read_bytes+6, 0,  (struct sockaddr *)&adresse_client_data, taille_adresse_data);
					}
					else {
						sent_bytes = sendto(socket_data, toSend[i], DATA_SIZE + 6, 0,  (struct sockaddr *)&adresse_client_data, taille_adresse_data);
					}
					if(sent_bytes < 0)
					{
						printf("Echec de l'envoi\n");
						exit(1);
					}
					else {
						start = clock();
						printf("Envoi du segment n°%d (%d octets envoyés)\n", numSeq+i, sent_bytes);
					}
				}
				
				bzero(buffer, sizeof(buffer));

				numSeq += nbSeg;

				//***** ACK management *****//

				clock_t start_timer;
				clock_t end_timer;
				double timer;
				int recv_bytes;

				start_timer = clock();	//On lance le timer
				
				timeoutReached = 0;
				
				printf("\n");
				
				//***** Réception des ACK *****//
				while (ackMaxRecu != numSeq-1) {
					do {
						
						recv_bytes = recvfrom(socket_data, buffer, sizeof(buffer), MSG_DONTWAIT, (struct sockaddr*)&adresse_client_data, &taille_adresse_data);
						end_timer = clock();
						timer = (double)(end_timer - start_timer)/CLOCKS_PER_SEC;
						
						if(timer > TIMEOUT_FACTOR * srtt){
							timeoutReached = 1;
							//***** On renvoie les segments non reçus par le client *****//.
							printf("\n     ***** TIMEOUT ! *****\n\n");
							printf("Les segments perdus entre %d et %d vont être renvoyés.\n\n", numSeq-nbSeg, numSeq-1);
								
								printf("Nouvel envoi des segments %d à %d\n\n", ackMaxRecu+1, numSeq);
								
								for(j = ackMaxRecu+1; j < numSeq; j++) {
									if(lastSegReached) {
										sendto(socket_data, toSend[j - numSeq + nbSeg], read_bytes + 6, 0,  (struct sockaddr *)&adresse_client_data, taille_adresse_data);
									}
									else {
										sendto(socket_data, toSend[j - numSeq + nbSeg], DATA_SIZE + 6, 0,  (struct sockaddr *)&adresse_client_data, taille_adresse_data);
									}
								}
								
							printf("\n");
							
							start_timer = clock();	//On relance le timer
						}
						
					} while(recv_bytes < 0);
					//***** ACK reçu *****//
					end = clock();
					rtt = (double)(end - start)/CLOCKS_PER_SEC;
					srtt = ALPHA * srtt + (1 - ALPHA) * rtt;
					/**printf("SRTT = %f, RTT = %f\n", srtt, rtt);*/

					//***** Extraction du numéro d'ACK *****//
					if (checkACK(buffer)) {
						char ack[6] = {0};
						memcpy(ack, buffer+3, 6);
						numAck = atoi(ack);
						printf("ACK n°%d reçu !\n", numAck);
					}
					
					//***** Enregistrement du numéro d'ACK *****//
					ackRecu = numAck;
					
					/********** FAST RETRANSMIT **********/
					if ( (ackRecu == dernierAckRecu) && (ackRecu == ackMaxRecu) )
					{
						nombre_de_fois_duplique++;
						if (nombre_de_fois_duplique >= 3)
						{
							nombre_de_fois_duplique = 0;
							printf("F-F-F-Fast Retransmiiiiiiiiiiiit !!!\n");
							printf("Le segment %d va être renvoyé.\n", ackRecu + 1);
							
							char numeroSeg[6] = {0};
							int numeroSegInt = 0;
							for(i = 0; i < MAXSEG; i++) {
								for(j = 0; j < 6; j++) {
									numeroSeg[j] = toSend[i][j];
								}
								numeroSegInt = atoi(numeroSeg);
								if(numeroSegInt == (ackRecu+1)) {
									numeroSegInt = i;
									break;
								}
							}
							
							int sent_bytes;
							sent_bytes = sendto(socket_data, toSend[numeroSegInt], read_bytes + 6, 0,  (struct sockaddr *)&adresse_client_data, taille_adresse_data);
							if(sent_bytes < 0)
							{
								printf("Echec de l'envoi\n");
								exit(1);
							}
							bzero(buffer, sizeof(buffer));
						}
					}
					dernierAckRecu = ackRecu;
					
					//***** On incrémente le nombre d'ACK reçus *****//
					if(ackRecu > ackMaxRecu) ackMaxRecu = ackRecu;					
					
				}
				printf("\n");
				
				if(timeoutReached) {
					if(nbSeg > 1) nbSeg /= 2;
				}
				else {
					if(nbSeg < MAXSEG) nbSeg *= 2;
				}
				bzero(toSend[0], sizeof(toSend[0]));
				
			}while(!feof(fp)); //On envoie des fichiers jusqu'à ce qu'un EOF soit atteint

			printf("Le fichier %s a été envoyé au client !\n\n", fileName);
			printf("Envoi de la séquence FIN ...\n");
			sendto(socket_data, "FIN", strlen("FIN") + 1, 0, (struct sockaddr*)&adresse_client_data, taille_adresse_data);
			printf("FIN envoyé\n");
			fclose(fp);
			run = 0;
		}
		else
		{
			//Erreur lors du fork
			printf("Fork raté !\n");
			return -1;
		}
	}
	return 0;
}
