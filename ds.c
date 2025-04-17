#include <arpa/inet.h>
#include <sys/types.h>
#include <time.h>
#include <math.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h> /* memset */
#include <unistd.h> /* close */
#include <stdio.h> /* printf */
#include <stdlib.h> /* exit */

#define BUFLEN 256 //dimensione massima di un messaggio di rete
#define STDIN 0 //numero di descrittore dello stdin
#define MSG_PEERLIST 1 //numero del tipo di messaggio peerlist
#define MSG_CONN_REQ 2 //numero del tipo di messaggio conn_req
#define MSG_DISC 3 //numero del tipo di messaggio di disconnessione
#define MSG_DISC_OK 4 //numero del tipo di messaggio di risposta ad una richiesta di disconnessione da parte di un peer al server
#define MSG_CLOSE_REQ 5 // richiesta chiusura dal server (non implementata)
#define MSG_TOOMANY 99 // numero del tipo di mesaggio di registrazione rifiutata per numero massimo raggiunto


struct neighbor //un neighbor si identifica tramite una porta (l'indirizzo ip è lo stesso per tutti dato che siamo sulla stessa macchina) e la distanza che ha da un peer 
		//preso in considerazione
{
	uint16_t port;
	unsigned int distanza;
};
struct peer // un peer si identifica con il numero di porta e i suoi neighbors
{
	uint16_t port;		
	uint16_t neighbor[2];
};

void showHelp() //funzione per mostrare a video i comandi che il ds può eseguire
{
	printf("\n1) help --> mostra i dettagli dei comandi\n");
	printf("2) showpeers --> mostra un elenco dei peer connessi\n");
	printf("3) showneighbor [<peer>] --> mostra i neighbor di un peer (opzionale)\n");
	printf("4) esc --> chiude il DS\n\n");
}

//void showPeers(uint16_t* list, int npeers) //funzione che mostra a video l'elenco dei peers, stampando le porte
void showPeers(struct peer* list, int npeers) //funzione che mostra a video l'elenco dei peers, stampando le porte
{
	int i;
	
	printf("Elenco dei peers:\n");
	for(i=0; i<npeers; i++)
	{
		//printf("\t%d\n", list[i]);
		printf("\t%d\n", list[i].port);
	}
	printf("\n");
}

//bool isReg(uint16_t* peers, int npeers, uint16_t peer_port) //funzione che controlla se un peer che ha fatto la richiesta di registrazione sia già registrato
bool isReg(struct peer* peers, int npeers, uint16_t peer_port) //funzione che controlla se un peer che ha fatto la richiesta di registrazione sia già registrato
{
	int i;
	for(i=0; i<npeers; i++)
	{
		//if(peers[i]==peer_port)
		if(peers[i].port==peer_port)
			return true;
	}
	return false;
}

//bool deReg(uint16_t* peers, int npeers, uint16_t peer_port) //funzione che rimuove un peer dalla lista dei peer registrati
bool deReg(struct peer* peers, int npeers, uint16_t peer_port) //funzione che rimuove un peer dalla lista dei peer registrati
{
	int i, j;
	for(i=0; i<npeers; i++)
	{
		//if(peers[i]==peer_port)
		if(peers[i].port==peer_port)
		{
			for(j=i; j<npeers-1; j++)
				peers[j] = peers[j+1];
			return true;
		}
	}
	return false;
}

void addNeighbors(uint16_t* n, struct peer* peers, int curr_peer) // aggiunge i neighbora al peer nella struttura
{
	peers[curr_peer].neighbor[0]=n[0];
	peers[curr_peer].neighbor[1]=n[1];
}

bool isNeighbor(struct peer* peers, int npeers, uint16_t curr_peer) // controlla se un peer è neighbor di almeno un altro peer
{
	int i;

	for(i=0;i<npeers; i++)
	{
		if(peers[i].neighbor[0]==curr_peer || peers[i].neighbor[1]==curr_peer)
			return true;
	}
	return false;
}

bool checkForNeighbor(struct peer* peers, int npeers, uint16_t checked, uint16_t checker) // controlla se checked è peer di checker
{
	int i;
	
	for(i=0;i<npeers;i++)
		if(peers[i].port==checker)
			if(peers[i].neighbor[0]==checked || peers[i].neighbor[1]==checked)
				return true;
	return false;
}

void replaceNeighbors(struct peer* peers, int npeers, uint16_t curr_peer)
{
	uint16_t p[20], temp;
	int n[20];
	int i, j, up=0, down=0;
	
	// disponi i peer ordinati in ordine crescente nella lista p
	for(i=0;i<npeers; i++)
	{
		p[i]=peers[i].port;
		for(j=i;j>0;j--)
		{
			if(p[j]<p[j-1])
			{
				temp=p[j-1];
				p[j-1]=p[j];
				p[j]=temp;
			}
		}
	}
	// controlla il numero di volte che un peer è neighbor
	for(i=0;i<npeers;i++)
	{
		n[i]=0;
		for(j=0;j<npeers;j++)
		{
			if(p[i]==peers[j].neighbor[0] || p[i]==peers[j].neighbor[1])
				n[i]++;
		}
	}
	// individuo il peer da cambiare
	for(i=0;i<npeers;i++)
	{
		if(p[i]==curr_peer)
			break;
	}
	// prendo il peer più vicino che è neighbor di almeno 2 peer
	up=down=i;
	temp=65535;
	do
	{
		up--;
		down++;
		if(down<npeers && n[down]>1) //considero solo i peer che sono neighbor di almeno due peer
			temp=p[down];
		if(checkForNeighbor(peers, npeers, temp, curr_peer)) // controllo che il candidato non sia neighbor del sostituendo, per evitare corto circuiti
			temp=65535;
		if(up>=0 && n[up]>1 && temp>p[up]) //considero solo i peer che sono neighbor di almeno due peer
			temp=p[up];
		if(checkForNeighbor(peers, npeers, temp, curr_peer)) // controllo che il candidato non sia neighbor del sostituendo, per evitare corto circuiti
			temp=65535;
		if(temp!=65535) //il primo che trovo esco
			break;
	} while(up>=0 || down<npeers);
	if(temp!=65535) // ho il mio candidato
	{
		for(i=0;i<npeers;i++)
		{
			if(peers[i].neighbor[0]==temp && peers[i].port!=curr_peer) //escludo me stesso
			{
				peers[i].neighbor[0]=curr_peer;
				printf("Sostituito il neighbor %d con il neighbor %d al peer %d\n\tper mantenere la ciclicità del network\n\n", curr_peer, temp, peers[i].port);
				return;
			}
			if(peers[i].neighbor[1]==temp && peers[i].port!=curr_peer) //escludo me stesso
			{
				peers[i].neighbor[1]=curr_peer;
				printf("Sostituito il neighbor %d con il neighbor %d al peer %d\n\tper mantenere la ciclicità del network\n\n", curr_peer, temp, peers[i].port);
				return;
			}
		}
	}
	else
		printf("Non è possibile chiudere il network sul peer %d\n", curr_peer);
}


void checkNeighbors(struct peer* peers, int npeers) // controlla se una parte del network è isolato
{
	int i;
	
	if(npeers<2) // sicuramente almeno un neighbor è 0 perché non ce n'è
		return;
	for(i=0;i<npeers;i++)
	{
		if(!isNeighbor(peers, npeers, peers[i].port))
			replaceNeighbors(peers, npeers, peers[i].port);
	}
}

//void setNeighbors(uint16_t* n, uint16_t* peers, int npeers, int curr_peer) //funzione che calcola i neighbor (2) di un peer basandosi sulla distanza tra le porte:
void setNeighbors(uint16_t* n, struct peer* peers, int npeers, int curr_peer) //funzione che calcola i neighbor (2) di un peer basandosi sulla distanza tra le porte:
//i due peer che hanno la minor distanza dalla porta del peer preso in considerazione saranno i due neighbor; in caso di assenza di neighbor (uno o entrambi) si mette 0;
// parametri: array dei vicini (2), array dei peer registrati e loro totale, porta di cui controllare i vicini, client richiedente (NULL se non richiesta da nessuno)
/*
	L'array dei vicini è riempito inserendo nella posizione 0 il peer più vicino, e nella posizione 1 il secondo più vicino
	Se uno dei due valori non è impostato (0 o 1 peer nella lista dei peer registrati), è impostato a 0 per default
	(Se il client riceve un valore 0 non considererà questo come peer da contattare)
*/
{
	struct neighbor n1={.port=0, .distanza=65536};
	struct neighbor n2={.port=0, .distanza=65536};
	int i;
	uint16_t dist;
	
	for(i=0;i<npeers; i++)
	{
		if(i==curr_peer) // non controllo per il client richiedente
			continue;
		//dist=abs(peers[curr_peer]-peers[i]); // calcolo lo scostamento
		dist=abs(peers[curr_peer].port-peers[i].port); // calcolo lo scostamento
		if(dist<n1.distanza) // sposto il precedente valore più vicino alla posizione 1 e imposto il nuovo valore alla posizione 0
		{
			n2.distanza=n1.distanza;
			n2.port=n1.port;
			n1.distanza=dist;
			//n1.port=peers[i];
			n1.port=peers[i].port;
		}
		else if(dist<n2.distanza) // imposto il nuovo valore alla posizione 1
		{
			n2.distanza=dist;
			//n2.port=peers[i];
			n2.port=peers[i].port;
		}		
	}
	n[0]=n1.port;
	n[1]=n2.port;
}

//void showNeighbor(uint16_t* list, int npeers, int man) //funzione che mostra a video i neighbor di un peer che viene passato come parametro; in caso di assenza di parametro la
void showNeighbor(struct peer* list, int npeers, int man) //funzione che mostra a video i neighbor di un peer che viene passato come parametro; in caso di assenza di parametro la
//funzione restituisce l'elenco dei due neighbor per ogni peer registrato
{
	int i;
	//uint16_t neighbor[2];

	if(man==-1)
	{
		for(i=0; i<npeers; i++) // per ogni peer registrato calcolo la lista dei neighbor
		{	
			//setNeighbors(neighbor, list, npeers, i); 
			//printf("\tLista dei neighbors per il peer %d: %d %d\n", list[i], neighbor[0], neighbor[1]);
			printf("\tLista dei neighbors per il peer %d: %d %d\n", list[i].port, list[i].neighbor[0], list[i].neighbor[1]);
		}
	}
	else
	{
		for(i=0; i<npeers; i++) // per ogni peer registrato...
		{	
			//if(list[i]==man) // se è quello specificato calcolo la lista dei neghbor
			if(list[i].port==man) // se è quello specificato calcolo la lista dei neghbor
			{
				//setNeighbors(neighbor, list, npeers, i); 
				///printf("\tLista dei neighbors per il peer %d: %d %d\n", list[i], neighbor[0], neighbor[1]);
				printf("\tLista dei neighbors per il peer %d: %d %d\n", list[i].port, list[i].neighbor[0], list[i].neighbor[1]);
				return;
			}
		}
		printf("\tPeer attualmente non registrato\n");
	}
	printf("\n");
}

int main (int argc, char* argv[]) 
{
	int ret, sd, len, i, j, port;
	fd_set read_fds, master;
	//uint16_t peers[20];
	uint16_t peer_port;
	uint16_t neighbor[2];
	//uint16_t* neighbor; //non usata
	int fdmax, npeers=0;
	//uint32_t reg[10]; //non usata
	char cmd[20];
	char line[100];
	int man=0, num;
	//int nreg=0; //non usata
	char buf[BUFLEN];
	//char ip[INET_ADDRSTRLEN]; //non usata 
	struct sockaddr_in my_addr, cl_addr;
	unsigned int addrlen=sizeof(struct sockaddr);
	//time_t rawtime;
	//struct tm* timeinfo;
	struct smsg //struttura di ogni messaggio di rete in una connessione UDP tra il ds e un peer
	{
		unsigned char type;
		uint16_t body;
	};
	struct smsg msg;
	struct peer peers[20];
	
	if(argc!=2)
		port=4242;
	else
		port=atoi(argv[1]);
	//printf("Uso la porta %d\n", port);
	sd=socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0); //socket non bloccanti
	//printf("Socket %d\n", sd);
	memset(&my_addr, 0, addrlen);
	my_addr.sin_family=AF_INET;
	my_addr.sin_port=htons(port);
	my_addr.sin_addr.s_addr=INADDR_ANY; //ascolto da qualunque indirizzo
	ret=bind(sd, (struct sockaddr*)&my_addr, sizeof(my_addr));
	//printf("Bind %d\n", ret);
	if(ret==-1)
	{
		perror("Errore di bind");
		exit(-1);
	}
	printf("\n**************** DS COVID STARTED ****************\n");
	printf("Digita un comando:\n");
	showHelp();
	FD_ZERO(&master);
	FD_ZERO(&read_fds);
	FD_SET(STDIN, &master);
	FD_SET(sd, &master);
	fdmax=sd;

	while(1)
	{
		//printf("Select\n");
		read_fds=master;
		select(fdmax+1, &read_fds, NULL, NULL, NULL);
		for(i=0; i<=fdmax; i++)
		{
			//printf("------------------------------------- %d\n", i);
			if(FD_ISSET(i, &read_fds))
			{
				if(i==sd)
				{
					//printf("Ricevo\n");	
					
					len=recvfrom(sd, buf, BUFLEN, 0, (struct sockaddr*)&cl_addr, &addrlen);
					if(len!=BUFLEN)
					{
						perror("Errore in receivefrom");
						break;
					}
					//strcpy(buf, "Ti mando la lista dei neighbor\n");
					//printf("Invio\n");
					sscanf(buf, "%d %d", (int*)&msg.type, (int*)&msg.body);
					peer_port=msg.body;
					if(msg.type==MSG_CONN_REQ)
					{
						if(!isReg(peers, npeers, peer_port))
						{
							if(npeers<20)
								//peers[npeers++]=peer_port; // registro il nuovo peer
								peers[npeers++].port=peer_port; // registro il nuovo peer
							else
							{
								//invia errore 
								sprintf(buf, "%d", MSG_TOOMANY);
								cl_addr.sin_port=htons(peer_port);
								len=sendto(sd, buf, BUFLEN, 0, (struct sockaddr*)&cl_addr, addrlen);				
								if(len!=BUFLEN)
								{
									perror("Errore in sendto");
								}
								continue; //ritorna alla prossima iterazione del for della select
							}
						}
						for(j=0; j<npeers; j++) // per ogni peer registrato calcolo la lista dei neighbor
						{	
							setNeighbors(neighbor, peers, npeers, j);
							addNeighbors(neighbor, peers, j); // aggiunge i neighbors al peer nella struttura
						}
						checkNeighbors(peers, npeers); // controlla se una parte del network è isolato
						for(j=0; j<npeers; j++) // per ogni peer registrato invio la lista dei neighbor
						{
							//sprintf(cmd, "%d %d", neighbor[0], neighbor[1]); //preparo buf convertendo in stringhe i numeri
							//sprintf(buf, "%d %d %d", MSG_PEERLIST, neighbor[0], neighbor[1]); // aggiungo il tipo messaggio come prima parola 
							sprintf(buf, "%d %d %d", MSG_PEERLIST, peers[j].neighbor[0], peers[j].neighbor[1]); 
								// aggiungo il tipo messaggio come prima parola 
							//cl_addr.sin_port=htons(peers[j]);
							cl_addr.sin_port=htons(peers[j].port);
							len=sendto(sd, buf, BUFLEN, 0, (struct sockaddr*)&cl_addr, addrlen);				
							if(len!=BUFLEN)
							{
								perror("Errore in sendto");
							}
						}	
						//close(i);
						//FD_CLR(i, &master);
						//printf("Esco\n");	
					}
					else if(msg.type==MSG_DISC)
					{
						if(deReg(peers, npeers, peer_port))
						{
							sprintf(buf, "%d", MSG_DISC_OK);
							cl_addr.sin_port=htons(peer_port);
							len=sendto(sd, buf, BUFLEN, 0, (struct sockaddr*)&cl_addr, addrlen);				
							if(len!=BUFLEN)
							{
								perror("Errore in sendto");
							}
							npeers--;
							for(j=0; j<npeers; j++) // per ogni peer registrato ricalcolo e invio la lista dei neighbor
							{	
								setNeighbors(neighbor, peers, npeers, j);
								addNeighbors(neighbor, peers, j); // aggiunge i neighbors al peer nella struttura
								checkNeighbors(peers, npeers); // controlla se una parte del network è isolato
								sprintf(buf, "%d %d %d", MSG_PEERLIST, peers[j].neighbor[0], peers[j].neighbor[1]); 
									// aggiungo il tipo messaggio come prima parola 
								//cl_addr.sin_port=htons(peers[j]);
								cl_addr.sin_port=htons(peers[j].port);
								len=sendto(sd, buf, BUFLEN, 0, (struct sockaddr*)&cl_addr, addrlen);				
								if(len!=BUFLEN)
								{
									perror("Errore in sendto");
								}
							}	
						}
					}
				}
				else if(i == STDIN)
				{
					char temp[10];
					
					// scanf("%[^\n]", cmd);
					//printf("%d", sscanf("%s", cmd));
					fgets(line, 100, stdin);
					//num=sscanf(line, "%s %d %*s", cmd, &man);
					num=sscanf(line, "%s %s", cmd, temp);
					man=atoi(temp);
					if(num==1 && strcmp(cmd, "help") == 0) 
						showHelp();
					else if(num==1 && strcmp(cmd, "esc") == 0) 
						exit(-1);
					else if(num==1 && strcmp(cmd, "showpeers") == 0)
						showPeers(peers, npeers);						
					else if((num==1 || (num==2 && man>0)) && strcmp(cmd, "showneighbor") == 0)
					{
						if(num==2)
							showNeighbor(peers, npeers, man);
						else if(num==1 && man>0)
							showNeighbor(peers, npeers, -1);
					}
					/*
					//richiesta di chiusura ai peer registrati: non implementata perché non indicate le specifiche
					else if(num==1 && strcmp(cmd, "???") == 0) 
					{
						sprintf(buf, "%d", MSG_CLOSE_REQ);
						for(j=0; j<npeers; j++)
						{	
							cl_addr.sin_port=htons(peers[j].port);
							len=sendto(sd, buf, BUFLEN, 0, (struct sockaddr*)&cl_addr, addrlen);				
							if(len!=BUFLEN)
							{
								perror("Errore in sendto");
							}
						}
					}
					*/
					else
						printf("Comando non riconosciuto, digitare help per l'elenco comandi\n\n");
					strcpy(line, "");
				}
			}
		}
	}
}

