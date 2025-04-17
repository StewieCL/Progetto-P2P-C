#include <arpa/inet.h>
#include <sys/types.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h> 
#include <unistd.h> 
#include <stdio.h> 
#include <stdlib.h>
#include <stdbool.h> 
#include <math.h> 
#include <sys/socket.h>
#include <errno.h>
 
#define BUFLEN 256
#define STDIN 0
#define MSG_PEERLIST 1
#define MSG_CONN_REQ 2
#define MSG_DISC 3
#define MSG_DISC_OK 4
#define MSG_CLOSE_REQ 5 // richiesta chiusura dal server (non gestita da server)
#define MSG_CONN_ACC 6
#define MSG_CONN_CLOSE 7
#define MSG_REQ_DATA 8
#define MSG_REPLY_DATA 9
#define MSG_FLOOD_FOR_ENTRIES 10
#define MSG_RESP_FLOOD_FOR_ENTRIES 11
#define MSG_REQ_ENTRIES 12
#define MSG_REPLY_ENTRIES 13
#define MSG_TOOMANY 99

void showHelp()
{
	//printf("\n1) help --> mostra i dettagli dei comandi\n");
	printf("\n1) start <addr> <port> --> chiede la connessione al server\n\tcon indirizzo ip <addr> sulla porta <port>\n");
	printf("2) add <type> <quantity> --> registra un evento di tipo\n\t[T]amponi | [N]uovi casi per un totale di <quantity>\n");
	printf("3) get <aggr> <type> <period> --> calcola dati aggregati\n\t([T]otale | [V]ariazione) per il tipo\n\t(T | N) nel periodo (dd:mm:yyyy|*-dd:mm:yyyy|*)\n");
	printf("4) stop --> chiude il peer\n\n");
}


bool checkMonth(int month) //controlla che il numero del mese sia effettivamente compreso tra 1 e 12
{
	if(month>12 || month<1)
		return false;
	return true;
}


bool checkDM(char* day, char* month, char* year) //controlla la validità della combinazione giorno-mese includendo i casi in cui i mesi hanno 30 giorni e il caso dell'anno bisestile
{
	int n_day, n_month, n_year;
	n_day=atoi(day);
	n_month=atoi(month);
	n_year=atoi(year);
	if(!checkMonth(n_month)) //se il numero del mese non è compreso tra 1 e 12
		return false;
	if(n_day<1 || n_day>31) //se il numero del giorno non è compreso tra 1 e 31
		return false;
	if(n_month==4 || n_month==6 || n_month==9 || n_month==11 || n_month==2) //se il mese ha 30 giorni o è febbraio
	{
		if(n_day>30) //impossibile perchè siamo nei mesi con 30 giorni o in febbraio
			return false;
		if(n_month==2) //se il mese è febbraio controllo la bisestilità dell'anno
		{
			if((n_year%400==0) || ((n_year%100!=0) && (n_year%4==0))) //se l'anno è bisestile
				if(n_day>29) //febbraio deve avere 29 giorni
					return false;
			if(n_day>28) //altrimenti febbraio ne ha 28
				return false;
		}
	}
	return true; //in tutti gli altri casi la data è giusta
}


int send_msg (int sock, char* buf, char* errmsg)
{
	unsigned char len=strlen(buf)+1;
	char temp[BUFLEN];
	int ret;
	
//printf("scritto %d ..%s..\n", len, buf);	
	sprintf(temp, "%c%s", len, buf);
	strcpy(buf, temp);
	//printf("buf 2 %d%s %d..\n",len, buf, strlen(buf)+1);
	ret=send(sock, (void*)buf, strlen(buf)+1, 0); 
//printf("ret scritto %d\n", ret);	
	if(strcmp(errmsg, "")==0) // errore gestito dal caller
		return ret;
	if(ret == -1)
	{
		if(errno != EINPROGRESS) 
		{
			perror(errmsg);
		}
	}	
	return ret;
}


int recv_msg(int sock, char* buf, char* errmsg)
{
	unsigned char len;
	int ret;

	ret = recv(sock, &len, 1, 0); // leggo la lunghezza del prossimo messaggio
//printf("letto: %d %d\n", ret, len);	
	ret = recv(sock, (void*)buf, len, 0);
//printf("letto: %d %s\n", ret, buf);	
	if(strcmp(errmsg, "")==0) // errore gestito dal caller
		return ret;
	if(ret == -1)
	{
		if(errno != EINPROGRESS) 
		{
			perror(errmsg);
		}
	}	
	//printf("len %d %s\n", len, buf);
	return ret;
}


int checkLooping(char* peerList, uint16_t* n) // controlla se i miei 2 neighbors sono già nella catena dei peers di un flooding
/*
	ritorna: 0 (non ci sono) | 1 (c'è il primo) | 2 (c'è il secondo) | 3 (ci sono entrambi)
	la catena (lifo) è formata così: peer(x):sock(x) peer(x-1):sock(x-1) ... peer(1):sock(1) peer(0)
	il peer(0) è l'originatore del flood, il peer(x) è l'ultimo che sta inviando il flood (se la funzione non ritorna 3!)
	il sock(x) serve per le risposte, che devono transitare fino al peer(0) sul socket da cui sono arrivate (sock(x))
*/
{
	char temp[8], temp_port[5], buf[BUFLEN];
	int temp_ret=0;
	
	strcpy(buf, peerList); //lo copio su una variabile di appoggio buf per non sporcare la catena passata come parametro 
	strcat(buf, " ::"); // aggiungo un indicatore di fine buffer (utilizzato per uscire dal ciclo)
	while(true)
	{
		sscanf(buf, "%s %[^\n]", temp, buf);
		sscanf(temp, "%[^:]", temp_port);
		if(atoi(temp_port)==(int)n[0])
			temp_ret++;
		if(atoi(temp_port)==(int)n[1])
			temp_ret+=2;
		if(strcmp(buf, "::")==0) // controlle se fine buffer
			return temp_ret;
	}
}


int sockForResp(char* port) // ritorna 0 o il socket per la risposta e la porta senza socket (serve per stampe di messaggi)
{
	int socket=0;
	char temp[10];
	
	sscanf(port, "%[^:]%*c%d", temp, &socket); // dato nella forma peer:socket
	strcpy(port, temp); // restituisco la porta senza socket
	return socket;
}


void getData(int date, char* datum, char* log) // imposta datum con il dato dei totali letto nel file, o stringa vuota se non c'è
{
	char line[100], temp[10];
	int file_date;
	FILE* f_tot;
	
	strcpy(datum, "");
	f_tot =fopen(log, "r");
	if(f_tot==NULL)
	{
		if(errno==ENOENT) // non c'è il file dei totali
			return;
		perror("Errore di log: "); // altro errore
		exit(1);
	}
	while (fgets(line, sizeof(line), f_tot)) //finché ci sono dati nel file leggo, quando non ce ne sono più o ho trovato il dato, esce dal ciclo;
						//legge una linea per volta dal file e la mette dentro line
	{
		sscanf(line, "%[^,]", temp); //metto la data di line in temp (dati nel formato data,totT,totN)
		file_date = atoi(temp);
		if(date==file_date)
		{
			strcpy(datum, line);
			break;
		}	
	}
	fclose(f_tot);
}


bool checkPeriod(char* period, int* start, int* end, int closed) 
// controlla la validità delle dat di inizio e fine del comando get, e imposta i valori numerici start e end (nel formato yyyymmdd)
{
	char s[10], e[10];
	char temp1[4], temp2[2], temp3[2];
	int upper_bound;
	char curr_date_time[10];
	struct tm *ptm;
	
	time_t rawtime=time(NULL);
	ptm = localtime(&rawtime);
	strftime(curr_date_time, sizeof(curr_date_time), "%Y%m%d", ptm);
	if(!closed)
	{
		rawtime-=3600*24; // la data limite per la validità della get come upper bound è ieri
		ptm=localtime(&rawtime);
		strftime(curr_date_time, sizeof(curr_date_time), "%Y%m%d", ptm);
	}
	upper_bound=atoi(curr_date_time); // limite massimo per richieste get
	*start=*end = -1;
	sscanf(period, "%[^-]%*c%s", s, e); //separo le due date per controllarle
	if(strcmp(s, "*")==0) //non esiste una data che fa da lower bound
		*start = 0;
	if(strcmp(e, "*")==0) //non esiste una data che fa da upper bound
		*end = upper_bound; // ci metto il massimo accettabile
	if(*start==-1 && strlen(s)!=10) //se esiste una data che fa da lower bound ma è scritta in un formato non consono ritorna
		return false;
	if(*end==-1 && strlen(e)!=10) //se esiste una data che fa da upper bound ma è scritta in un formato non consono ritorna
		return false;
	if(*start==-1) /*se esiste una data che fa da lower bound ed è scritta in un formato consono,
			controlla che il numero del giorno vada da 1 a 31, il numero del mese vada da 1 a 12 e il numero dell'anno sia di 4 cifre*/
	{
		sscanf(s, "%[^:]%*c%[^:]%*c%s", temp1, temp2, temp3); //scompongo la prima data in temp1-->giorno(dd), temp2-->mese(mm), temp3-->anno(yyyy)
		if(strlen(temp1)!=2 || strlen(temp2)!= 2 || strlen(temp3)!=4)
			return false;
		if(!checkDM(temp1, temp2, temp3))
			return false;
		sprintf(s, "%s%s%s", temp3, temp2, temp1);
		*start = atoi(s); //trasformo la data da stringa a intero per migliore gestione con l'operazione di differenza
	}
	if(*end==-1) /*se esiste una data che fa da lower bound ed è scritta in un formato consono, 
			controlla che il numero del giorno vada da 1 a 31, il numero del mese vada da 1 a 12 e il numero dell'anno sia di 4 cifre*/
	{
		sscanf(e, "%[^:]%*c%[^:]%*c%s", temp1, temp2, temp3); //scompongo la prima data in temp1-->giorno(dd), temp2-->mese(mm), temp3-->anno(yyyy)
		if(strlen(temp1)!=2 || strlen(temp2)!= 2 || strlen(temp3)!=4)
			return false;
		if(!checkDM(temp1, temp2, temp3))
			return false;
		sprintf(e, "%s%s%s", temp3, temp2, temp1);
		*end=atoi(e);
	}
	if(*end>upper_bound) // data successiva all'ultima utilizzabile (se la seconda data nella get è oggi prima della chiusura o è un giorno futuro)
		return false;
	if(*end<*start) // il formato delle due date è corretto ma la prima è maggiore della seconda, ritorna falso
		return false;
	return true;
}


void sort(char* filename) // ordina il file dei totali usando il comando di sistema
{
	char command[100];

	strcpy(command, "sort "); // costruisco il comando di sistema sort fIN -o fOUT (genero il file ordinato)
	strcat(command, filename);
	strcat(command, " -o ");
	strcat(command, filename);
	strcat(command, ".sorted"); 
	system(command); // lancio il comando
	strcpy(command, "cp "); // costruisco il comando di sistema cp fOUT fIN (copio il file ordinato sull'originale)
	strcat(command, filename);
	strcat(command, ".sorted ");
	strcat(command, filename);
	system(command); // lancio il comando
	strcpy(command, filename); // cancello il file temporaneo ordinato (riutilizzo la variabile command)
	strcat(command, ".sorted");
	remove(command);
}


bool writeToFile(char* log_name, char* dato, int pending_req) // restituisce true se il dato era quello richiesto, e quindi è scritto nel file; false altrimenti
{
	FILE* total;
	int n_quantity_req, t_quantity_req;
	char date_req[8], st_quantity_req[4];

	sscanf(dato, "%[^,]%*c%[^,]%*c%d", date_req, st_quantity_req, &n_quantity_req);
	t_quantity_req=atoi(st_quantity_req);							
	if(atoi(date_req)!=pending_req)
		return false; //il dato non è quello attualmente richiesto, lo scarto
	total=fopen(log_name, "a");
	if(total==NULL)
	{
		perror("Errore di log");
		exit(1);
	}
	//printf("\t\tdata %s, tot_tamponi %d, tot_nuovicasi %d\n", date_req, t_quantity_req, n_quantity_req); 
	fprintf(total, "%s,%d,%d\n", date_req, t_quantity_req, n_quantity_req);
	fclose(total);	
	sort(log_name); // rimetto in ordine il file	
	return true;					
}


void calculate(char* log_name, char* tot_log_name, int day)
/*
	calcola i totali leggendo dal file giornaliero solo se day > data nel file (altrimenti non è ancora stata chiusa la giornata
	e si scriverebbero due record con la stessa data nel file dei totali (questo perché all'inizio del programma viene lanciata una calculate per scrivere
	i dati parziali di una gionrata precedente eventualmente rimasti non scritti; in questo modo nel file giornaliero sicuramente ci saranno sempre
	i dati relativi ad una sola giornata)
*/
// apro in lettura log_name, in append tot_log_name, leggo, conteggio, scrivo, cancello log_name
{
	FILE* dayly;
	FILE* total;
	char date[50], squantity[50], type[2], line[100], y[4], d[2], m[2];
	int quantity, n_quantity = 0, t_quantity = 0; // contatori per nuovi casi o tamponi
	
	if(!(dayly=fopen(log_name, "r"))) 
		return; /* il file giornaliero non esiste. Dal momento che quando il programma si avvia viene lanciata questa fuinzione per calcolare
			eventuali registrazioni giornaliere rimaste per un precedente crash del peer, questa è la condizione normale se tutto
			era stato chiuso correttamente */
	if(dayly==NULL)
	{
		perror("Errore di log");
		exit(1);
	}
	total=fopen(tot_log_name, "a");
	if(dayly==NULL)
	{
		perror("Errore di log");
		exit(1);
	}
	
	while (fgets(line, sizeof(line), dayly)) // finché ci sono dati nel file leggo, quando non ce ne sono più esce dal ciclo; 
						// legge una linea per volta dal file e la mette dentro line
	{
		//sscanf(line, "%[^,]%*c%c%*c%d", date, type, &quantity);
		sscanf(line, "%[^:]%*c%[^:]%*c%s", y, m, d);
		sprintf(date, "%s%s%s", y, m, d);
		if(atoi(date)==day)
		{
			// ho chiuso e riaperto il peer, ma la giornata non è ancora terminata
			fclose(dayly);
			fclose(total);
			return;
		}
		sscanf(line, "%[^,]%*c%1s%*c%s", date, type, squantity); //distribuisco nelle 3 variabili il dato inserito nelle add di oggi, che sta dentro line
		quantity = atoi(squantity);
		if(strcmp(type, "N")==0)
			n_quantity+=quantity; //incremento il totale dei nuovi casi
		else if(strcmp(type, "T")==0)
			t_quantity+=quantity; //incremento il totale dei tamponi
	}
	strcpy(line, date);
	sscanf(line, "%[^:]%*c%[^:]%*c%s", y, m, d);
	sprintf(date, "%s%s%s", y, m, d);
	fprintf(total, "%s,%d,%d\n", date, t_quantity, n_quantity); //????
	fclose(dayly);
	fclose(total);
	remove(log_name); // cancello il giornaliero
	sort(tot_log_name); // ordino il file dei totali per data
}

void dateToStr(int iDate, char* sDate) // trasforma una data in formato yyyymmdd nel formato dd/mm/yyyy (fomrato leggibile usato nei messaggi)
{
	int y, m, d;

	d = iDate % 100;
	iDate /= 100;
	m = iDate % 100;
	y = iDate / 100;
	sprintf(sDate, "%d/%d/%d", d, m, y);
	
}	

/* NON UTILIZZATA
int daysDiff(int d1, int d2) // calcola i giorni tra due date
{
	int y, m, d;
	struct tm *ptm1, *ptm2;
	time_t rawtime1, rawtime2;
		
	d = d1 % 100;
	d1 /= 100;
	m = d1 % 100;
	y = d1 / 100;
	rawtime1=time(NULL);
	ptm1 = localtime(&rawtime1); // imposto la struttura con una data corretta	
	ptm1->tm_year = y-1900; // anno dal 1900
	ptm1->tm_mday = d;
	ptm1->tm_mon = m-1; // mesi 0..11
	ptm1->tm_sec = 0;
	ptm1->tm_min = 0;
	ptm1->tm_hour = 0;
	rawtime1 = mktime(ptm1); // metto in un time_t (numero di secondi dal 1970)

	d = d2 % 100;
	d2 /= 100;
	m = d2 % 100;
	y = d2 / 100;
	rawtime2=time(NULL);
	ptm2 = localtime(&rawtime2); // imposto la struttura con una data corretta	
	ptm2->tm_year = y-1900;
	ptm2->tm_mday = d;
	ptm2->tm_mon = m-1;
	ptm2->tm_sec = 0;
	ptm2->tm_min = 0;
	ptm2->tm_hour = 0;
	rawtime2 = mktime(ptm2);
	rawtime1 = rawtime2-rawtime1;
	return rawtime1 / (24*3600); // tolgo hms
}
*/
	
int getNextDay(int day) // calcola il giorno successivo a day
{
	struct tm *ptm;
	int y, m, d;	
	char date[10]; 
	time_t rawtime;
	
	d = day % 100;
	day /= 100;
	m = day % 100;
	y = day / 100;
	rawtime=time(NULL);
	ptm = localtime(&rawtime); // imposto la struttura con una data corretta	
	ptm->tm_year = y-1900; // anno dal 1900
	ptm->tm_mday = d;
	ptm->tm_mon = m-1; // mesi 0..11
	rawtime = mktime(ptm); // metto in un time_t (numero di secondi dal 1970)
	rawtime += 24*3600; // aggiungo 1 giorno in secondi
	ptm = localtime(&rawtime); // rileggo la data
	d = ptm->tm_mday;
	m = ptm->tm_mon+1;
	y = ptm->tm_year;
	strftime(date, sizeof(date), "%Y%m%d", ptm);
	return atoi(date);
}


bool add(char type, int quantity, bool closed, char* log_name, char* tot_log_name) // closed 0 | 1 se il giorno è già stato chiuso
// scrive nel file giornaliero i dati dal comando add nel formato yyyy:mm:dd,type,quantity (type T | N); se è il caso, prima chiudo la giornata chiusa e calcolo i totali
{
	FILE* log;
	struct tm *ptm;
	char curr_date_time[50];
	int temp;
	int ret=closed; // se ho già fatto una chiusura prima, faccio in modo che ritorni true

	time_t rawtime=time(NULL);
	ptm = localtime(&rawtime);
	strftime(curr_date_time, sizeof(curr_date_time), "%H", ptm);
	if(atoi(curr_date_time)>17 || closed)
	{
		strftime(curr_date_time, sizeof(curr_date_time), "%Y%m%d", ptm);
		temp = atoi(curr_date_time);
		dateToStr(temp, curr_date_time);
		printf("\tCalcolo i totali del giorno %s\n", curr_date_time);
		rawtime += 3600*24; // vado al giorno dopo
		ptm = localtime(&rawtime);
		strftime(curr_date_time, sizeof(curr_date_time), "%Y%m%d", ptm);
		calculate(log_name, tot_log_name, atoi(curr_date_time)); // se !closed, sicuramente !calculated
		ret=true; // segnalo che ho fatto io il calcolo
		printf("\tRegistrazioni chiuse per il giorno corrente; registro in data successiva\n");
	}
	log=fopen(log_name, "a");
	if(log==NULL)
	{
		perror("Errore di log");
		exit(1);
	}
	strftime(curr_date_time, sizeof(curr_date_time), "%Y:%m:%d", ptm);
	fprintf(log, "%s,%c,%d\n", curr_date_time, type, quantity);
	printf("\tRegistrazione effettuata: tipo %c, quantità %d\n", type, quantity); 
	fclose(log);
	return ret; // true se ho fatto il calcolo, false altrimenti
}


void get(int sock1, int sock2, int tcp_comm1_en, int tcp_comm2_en, char* log_file, int start, int end, char aggr, char type, int* pending, int* pending_res, int orig_start)
/*
	controlla se nel file dei totali ci sono le date tra start e end; alla prima data mancante lancia una REQ_DATA ai neighbor (se ci sono)
	se le date ci sono tutte, rilegge il file e calcola il dato richiesto (aggr per type).
	pending è la data mancante che viene richiesta ai neighbors; se i dati per la data sono reperiti in rete, la funzione viene chiamata successivamente con start = il
	giorno successivo a pending; alla prima data mancante il comando viene abortito (registrando però gli eventuali dati reperiti in rete fino a quel momento
	(da start a pending-1)). 
	orig_start è la data iniziale richiesta originariamente a tastiera, utilizzata per il calcolo se ci sono tutti i dati.
	pending_res è il numero di risposte che sto aspettando dalla rete; qui viene incrementato ogni volta che faccio una REQ_DATA
*/
{
	char buf[BUFLEN], line[100], temp[10];
	int date=-1;
	//int start1=start;
	FILE* f_tot;
	bool needData=false;
	int totale=0, variazione=(2^31)-1; // imposto al valore massimo
	int local_N, local_T;
	//time_t start_t=time(NULL);

	f_tot = fopen(log_file, "r");
	if(f_tot==NULL)
	{
		if(errno!=ENOENT) // ENEDNT = non c'è il file dei totali
		{
			perror("Errore di log");
			exit(1);
		}
		else
		{
			if(start==0) // ho indicato il periodo nel formato *-?
			{
				printf("\tNon ci sono date registrate; data inizio non identificabile\n");
				return;
			}
			needData=true;
		}
	}
	if(start<=end && start!=date) // se la prima è falsa vuol dire che l'ultimo dato necessario mi è arrivato da un neighbor; la seconda che c'è il file
	{	
		if(errno!=ENOENT)
		{
			while (fgets(line, sizeof(line), f_tot)) // finché ci sono dati nel file leggo, quando non ce ne sono più esce dal ciclo; 
								// legge una linea per volta dal file e la mette dentro line
			{
				sscanf(line, "%[^,]", temp); //metto la data di line in temp
				date = atoi(temp);
				if(start==0)
					start = date; // la prima data del periodo è la mia prima disponibile
				if(date<start) // scarto le date registrate anteriori alla richiesta
					continue;
				if(date==start && date!=end)
				{
					start=getNextDay(start); //ho il dato nel log dei totali, continuo a scorrere
					continue;
				}
				else if(date>start) //mi manca un dato perchè ho letto una data successiva a quella che volevo
				{
					date=start; 
					needData=true;
					break;
				}
				if(date==end)
					break;
			}
		}
		if(date==-1) // il file è vuoto o non c'è
		{
			if(start==0) // ho indicato il periodo nel formato *-?
			{
				printf("\tNon ci sono date registrate; data inizio non identificabile\n");
				return;
			}
			date=start;
			needData=true;
			//break; 
		}
		if(date<end && !needData)
		{
			date=getNextDay(date);
			needData=true;
		}
		// se needData devo fare le send ai neighbor
		if(needData)
		{
			*pending_res=0;
			dateToStr(date, temp);
			if(tcp_comm1_en || tcp_comm2_en)
			{
				*pending=date;
				if(tcp_comm1_en)
					*pending_res = *pending_res+1;				
				if(tcp_comm2_en)
					*pending_res = *pending_res+1;				
				printf("\tDato del totale mancante per il giorno %s;\n\t\trichiesto dato ai neighbor\n", temp);
			}
			else
			{
				printf("\tDato del totale mancante per il giorno %s;\n\t\tnon ci sono neighbor disponibili per richiederlo\n", temp);
				return;
			}
			//sprintf(buf, "%d %d", MSG_REQ_DATA, date); //preparo buf convertendo in stringhe i numeri
			if(tcp_comm1_en)
			{
				sprintf(buf, "%d %d", MSG_REQ_DATA, date); //preparo buf convertendo in stringhe i numeri
				//ret=send(sock1, (void*)buf, strlen(buf)+1, 0); 
				//ret=send_msg(sock1, buf, "Errore in send sock1: "); 
				send_msg(sock1, buf, "Errore in send sock1: "); 
				/*
				if(ret == -1)
				{
					if(errno != EINPROGRESS) 
					{
						perror("Errore in send sock1: ");
					}
				}	
				*/		
			}
			if(tcp_comm2_en)
			{
				sprintf(buf, "%d %d", MSG_REQ_DATA, date); //preparo buf convertendo in stringhe i numeri
				//ret=send(sock2, (void*)buf, strlen(buf)+1, 0); 
				//ret=send_msg(sock2, buf, "Errore in send sock2: "); 
				send_msg(sock2, buf, "Errore in send sock2: "); 
				/*
				if(ret == -1)
				{
					if(errno != EINPROGRESS) 
					{
						perror("Errore in send sock2: ");
					}
				}
				*/			
			}
		}
	}  
	if(!needData) //ho tutte le date, devo fare i conteggi
	{
		char locT[4];
		
		*pending=0; // la richiesta get è stata onorata, non ci sono più date da chiedere
		rewind(f_tot);		
		start=orig_start; //rimetto la data di partenza richiesta dall'utente
		while (fgets(line, sizeof(line), f_tot)) // finché ci sono dati nel file leggo, quando non ce ne sono più esce dal ciclo; 
							// legge una linea per volta dal file e la mette dentro line 
		{
			sscanf(line, "%[^,]%*c%[^,]%*c%d", temp, locT, &local_N);
			local_T=atoi(locT);										
			date = atoi(temp);
			if(start==0)
				start = date; // la prima data del periodo è la mia prima disponibile
			if(date<start) // scarto le date registrate anteriori alla richiesta
				continue;
			if(date>end)
				break;
			if(aggr=='T')
			{
				//calcolo i totali
				if(type=='T')
					totale+=local_T;
				else
					totale+=local_N;
			}
			else
			{
				// calcolo le variazioni
				dateToStr(date, temp);
				if(type=='T')
				{
					if(variazione!=(2^31)-1)
						printf("\tVariazione del giorno %s rispetto al precedente per il tipo %c: %d\n", temp, type, local_T-variazione);
					variazione = local_T;
				}
				else
				{	
					if(variazione!=(2^31)-1)
						printf("\tVariazione del giorno %s rispetto al precedente per il tipo %c: %d\n", temp, type, local_N-variazione);
					variazione = local_N;
				}
			}	
		}
	}
	if(f_tot!=NULL)
		fclose(f_tot);
	if(totale>0)
	{
		char date_s[10], date_e[10];
		dateToStr(orig_start, date_s);
		dateToStr(end, date_e);
		printf("\tTotali dal %s al %s per il tipo %c: %d\n", date_s, date_e, type, totale);
	}
}


void deReg(int sock, uint32_t sv_address, uint16_t sv_port, int peer_port, uint16_t* n) 
{
	struct sockaddr_in sv_addr;
	struct smsg
	{
		unsigned char type;
		uint16_t body;
	};
	struct smsg msg;
	unsigned int addrlen=sizeof(struct sockaddr);
	char buf[BUFLEN];
	int len, times=0, msg_type;

	memset(&sv_addr, 0, addrlen);
	sv_addr.sin_family=AF_INET;
	sv_addr.sin_addr.s_addr = sv_address;	
	sv_addr.sin_port=htons(sv_port);
	msg.type=MSG_DISC; 
	msg.body=peer_port;	
	sprintf(buf, "%d %d", msg.type, msg.body);
	while(1)
	{
		time_t start_t=time(NULL);
		len=sendto(sock, buf, BUFLEN, 0, (struct sockaddr*)&sv_addr, addrlen);
		if(len!=BUFLEN)
		{
			perror("Errore in sendto");
		}
		while(1)
		{
			//end_t = (double)clock(); // contollo il timer attuale
			//total_t = end_t - start_t;
			if (time(NULL) - start_t > 2) // se sono passati 2 secondi senza risposta, faccio un'altra richiesta
				break;
			len=recvfrom(sock, buf, BUFLEN, 0, (struct sockaddr*)&sv_addr, &addrlen); 
			if (len==BUFLEN) // risposta ricevuta dal server
			//{
				///msg_type = (checkInput(buf)); // controllo la correttezza della lunghezza
				//if(msg_type == 0)
				//{
			//	perror("Errore in receivefrom: ");
			//	return 0;
			//}
			{			
				sscanf(buf, "%d", &msg_type);
				if(msg_type==MSG_DISC_OK)
				{	
					printf("\tDisconnessione effettuata con successo\n");
					return;
				}
			}
		}
		times++;
		if (times==10) // permetto al massimo 10 tentativi di connessione al server
		{
			printf("Tempo per i tentativi di connessione scaduto, il server non risponde;\n\tcomando abortito\n");
			return;
		}
		printf ("Timeout scaduto senza risposta; invio ancora richiesta...\n");
	}

}

bool boot(int sock, char* sv_address, char* sv_port, int peer_port, uint16_t* n)
{
	struct sockaddr_in sv_addr;
	struct smsg
	{
		unsigned char type;
		uint16_t body;
	};
	struct smsg msg;
	//uint16_t msg;
	int len, msg_type, times=0;
	unsigned int addrlen=sizeof(struct sockaddr);
	char buf[BUFLEN];
	//double start_t, end_t, total_t;
	
	memset(&sv_addr, 0, addrlen);
	sv_addr.sin_family=AF_INET;
	inet_pton(AF_INET, sv_address, &sv_addr.sin_addr);	
	sv_addr.sin_port=htons(atoi(sv_port));
	msg.body=peer_port;
	msg.type=MSG_CONN_REQ; 
	sprintf(buf, "%d %d", msg.type, msg.body);	
	while(true)
	{
		//struct tm *ptm;
		
		time_t start_t=time(NULL);
		//ptm = localtime(&start_t);
		len=sendto(sock, buf, BUFLEN, 0, (struct sockaddr*)&sv_addr, addrlen);
		if(len!=BUFLEN)
		{
			perror("Errore in sendto");
		}
		//start_t = (double)clock(); // inizializzo il timer per controllare il timeout di risposta
		while(true)
		{
			//end_t = (double)clock(); // contollo il timer attuale
			//total_t = end_t - start_t;
			if (time(NULL) - start_t > 2) // se sono passati 2 secondi senza risposta, faccio un'altra richiesta
				break;
			len=recvfrom(sock, buf, BUFLEN, 0, (struct sockaddr*)&sv_addr, &addrlen); 
			if (len==BUFLEN) // risposta ricevuta 
			//{
				//perror("Errore in receivefrom: ");
				//return 0;
			//}
			//else
			{
				sscanf(buf, "%d %d %d", &msg_type, (int*)&n[0], (int*)&n[1]);
				//sscanf(buf, "%d %d %d", &msg_type, &n[0], &n[1]); prova con questa
				if(msg_type==MSG_PEERLIST)
					return 1;
				else if(msg_type==MSG_TOOMANY)
				{
					printf("Il server ha raggiunto il numero massimo di peer registrabili;\n\tconnessione rifiutata\n");
					return 0;
				}
				else
				{
					printf("Il server ha risposto con un messaggio sconosciuto: %s", buf);
					return 0;
				}
			}
		}
		times++;
		if (times==10) // permetto al massimo 10 tentativi di connessione al server
		{
			printf("Tempo per i tentativi di connessione scaduto; comando abortito\n");
			return 0;
		}
		printf ("Timeout scaduto senza risposta; invio ancora richiesta...\n");
	}
}


void azzera_Sock(int* sock_com)
{
	int i;
	for(i=0; i<17; i++)
		sock_com[i] = -1;
}


int first_Sock_Free(int* sock_com)
{
	int i;
	for(i=0; i<17; i++)
	{
		if(sock_com[i]==-1)
			return i;
	}
	return -1; //se ritorna -1 vuol dire che i 17 socket sono tutti occupati (impossibile)-->cosa faccio nel main????????
}


int main (int argc, char* argv[]) 
{
	struct smsg
	{
		unsigned char type; 
		char body[1023];
	};
	struct smsg msgst;
	int ret, sd, tcp_listener, tcp_communicator1, tcp_communicator2, tcp_flooder=-1, i, len, peer_port, msg_type, pending_req=0, pending_for_ask, pending_resp=0, req_data;
	bool tcp_communicator1_enabled = false, tcp_communicator2_enabled = false; // vero quando li uso
	//FILE* log;
	int startPeriod, endPeriod; // utilizzati nel comando get
	int sock_com[17];
	//int sock_pending_flood;
	const char* yes="yes";
	bool closed=false, calculated=false; // badLuck=false;
	int closed_date = 0; // se il server chiede una chiusura metto a true e salvo la data; se già calcolato per il giorno calculated = true
	//int temp_date; per pasaare a checkPeriod
	int hr; // per salvarmi l'ora corrente al controllo di chiusura
	fd_set read_fds, master;
	int quantity;
	uint16_t neighbor[2];
	//uint16_t peers[20], msg; 
	//int tcp_communicators[2]; // devo usare tanti socket quanti i neighbors
	//struct sockaddr n_addr[2]; // devo usare tanti addr quanti i neighbors
	int fdmax; 
	//int npeers=0;
	uint32_t srvaddr;
	//uint32_t reg[10]; 
	char cmd[50], param1[30], param2[30], param3[30], log_name[20], tot_log_name[20], req_data_date[10], req_data_data[30];
	char line[150], temp_msg[6];
	int num;
	//int nreg=0, man=0; 
	uint16_t recvport, srvport=0;
	char buf[BUFLEN];
	//char ip[INET_ADDRSTRLEN];
	struct sockaddr_in my_addr, sv_addr, peercl_addr, neigh_addr, flooder_addr;
	unsigned int addrlen=sizeof(struct sockaddr);
	char curr_date_time[50];
	char tempStr[50];
	//bool neigh1_data=false, alreadyFLOODed=false; 
	char get_type, get_aggr;
	struct tm *ptm;
	
	if(argc!=2)
		peer_port=4242;
	else
		peer_port=atoi(argv[1]);		
	//printf("Uso la porta %d\n", peer_port);
	strcpy(req_data_data, "");
	sprintf(log_name, "%d.log", peer_port);
	sprintf(tot_log_name, "%d.tot.log", peer_port);
	time_t rawtime = time(NULL);
	ptm = localtime(&rawtime);
	strftime(curr_date_time, sizeof(curr_date_time), "%H", ptm);
	hr = atoi(curr_date_time); 
	strftime(curr_date_time, sizeof(curr_date_time), "%Y%m%d", ptm);	
	if (hr>17)
	{
		rawtime += 3600*24; // vado al giorno dopo
		ptm = localtime(&rawtime);
		strftime(curr_date_time, sizeof(curr_date_time), "%Y%m%d", ptm);
	}
	calculate(log_name, tot_log_name, atoi(curr_date_time)); // nel caso fossero rimaste appese registrazioni non calcolate, ma solo se giorno già chiuso
	//log=fopen(log_name, "a+");
	//fclose(log);
	sd=socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0); //socket udp
	tcp_listener=socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0); //socket di ascolto
	tcp_communicator1=socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0); //socket di comunicazione con il neighbor1
	tcp_communicator2=socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0); //socket di comunicazione con il neighbor2
	//tcp_flooder=socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0); // socket per i dati flood
	//printf("Socket %d\n", sd);
	memset(&sv_addr, 0, addrlen);
	memset(&my_addr, 0, addrlen);	
	memset(&neigh_addr, 0, addrlen);	
	sv_addr.sin_family=AF_INET;
	my_addr.sin_family=AF_INET;	
	neigh_addr.sin_family=AF_INET;	
	my_addr.sin_port=htons(peer_port);
	my_addr.sin_addr.s_addr=INADDR_ANY;
	neigh_addr.sin_addr.s_addr=my_addr.sin_addr.s_addr;
	flooder_addr.sin_family=AF_INET;
	flooder_addr.sin_addr.s_addr=my_addr.sin_addr.s_addr;
	ret=bind(sd, (struct sockaddr*)&my_addr, sizeof(my_addr));
	//printf("Bind %d\n", ret);
	if(ret==-1)
	{
		perror("Errore di bind UDP ");
		exit(1);
	}
	if(setsockopt(tcp_listener, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)) == -1) // altrimenti dà errore Socket already in use anche se il programma esce bene
	{
		perror("errore di setsockopt()");
		exit(1);
	}
	/*
	if(setsockopt(tcp_communicator1, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)) == -1) // altrimenti dà errore Socket already in use anche se il programma esce bene
	{
		perror("errore di setsockopt()");
		exit(1);
	}
	if(setsockopt(tcp_communicator2, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)) == -1) // altrimenti dà errore Socket already in use anche se il programma esce bene
	{
		perror("errore di setsockopt()");
		exit(1);
	}
	*/
	/*
	if(setsockopt(tcp_flooder, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)) == -1) // altrimenti dà errore Socket already in use anche se il programma esce bene
	{
		perror("errore di setsockopt()");
		exit(1);
	}
	*/
	/**/
	ret=bind(tcp_listener, (struct sockaddr*)&my_addr, sizeof(my_addr));
	//printf("Bind %d\n", ret);
	if(ret==-1)
	{
		perror("Errore di bind del listener TCP ");
		exit(1);
	}	
	ret = listen(tcp_listener, 19);
	//printf("Ho fatto il listen %d\n", ret);
	if(ret==-1)
		exit(1);
	FD_ZERO(&master);
	FD_ZERO(&read_fds);
	FD_SET(STDIN, &master);
	FD_SET(sd, &master);
	FD_SET(tcp_listener, &master);
	if(tcp_listener>sd)
		fdmax=tcp_listener;
	else
		fdmax=sd;
	//if (fdmax<tcp_client)
	//	fdmax=tcp_client;
	showHelp();
	azzera_Sock(sock_com);

	while(true)
	{
		read_fds=master;
		select(fdmax+1, &read_fds, NULL, NULL, NULL);
		// appena la select si sblocca e ci sono dati in read_fds, mi calcolo la data e chiudo se necessario (o annullo la chiusura)
		time_t rawtime=time(NULL);
		ptm = localtime(&rawtime);
		strftime(curr_date_time, sizeof(curr_date_time), "%H", ptm);
		hr = atoi(curr_date_time); 
		strftime(curr_date_time, sizeof(curr_date_time), "%Y%m%d", ptm);
		if (hr>17 && closed==0) // chiusura per orario
		{
			closed = 1;
			closed_date = atoi(curr_date_time); // converto in numerico per gestirlo meglio
			printf("\tChiusura per raggiunto limite orario\n");
			if (!calculated) // se non ancora calcolato il totale giornaliero, calcolo
			{
				dateToStr(closed_date, tempStr);
				printf("\tCalcolo i totali del giorno %s\n", tempStr);
				rawtime += 3600*24; // vado al giorno dopo
				ptm = localtime(&rawtime);
				strftime(curr_date_time, sizeof(curr_date_time), "%Y%m%d", ptm);
				calculate(log_name, tot_log_name, atoi(curr_date_time));
				calculated = 1;
			}
		}
		else if (closed && atoi(curr_date_time)>closed_date) // la richiesta di chiusura non è per il giorno corrente ma antecedente
		{
			closed = 0; // la annullo
			closed_date = 0;
			printf("\tAnnullata richiesta di chiusura scaduta\n");
		}
		//printf("select\n");
		for(i=0; i<=fdmax; i++)
		{
			if(FD_ISSET(i, &read_fds))
			{
			//printf("sto analizzando il socket %d\n", i);
				//char buf[BUFLEN];

				if(i==tcp_listener) //sono il peer server e mi è arrivata una richiesta tcp da un peer client
				{
					//printf("passa da tcp_listener\n");
					int first;
					//time_t start_t=time(NULL);
					first=first_Sock_Free(sock_com);
					sock_com[first] = accept(tcp_listener, (struct sockaddr*)&peercl_addr, &addrlen); //apre un socket di comunicazione
					/*
					while(time(NULL) - start_t > 2) // do tempo 2 secondi per la risposta
					{
						if(sock_com[first]!=-1)
							break;
					}
					if(sock_com[first]==-1)
						break;
					*/
					//if(!isNeighbor(peercl_addr.sin_port, neighbor))
					//{
					//	printf("Il peer non è mio neighbor\n");
					//	break;
					//}
					printf("\ts Accetto connessioni TCP sul socket %d[%d] da %d\n", sock_com[first], first, ntohs(peercl_addr.sin_port));
					FD_SET(sock_com[first], &master);
					//FD_SET(tcp_communicator, &read_fds);
					if(fdmax<sock_com[first])
						fdmax=sock_com[first];
					//printf("fdmax vale %d\n", fdmax);
					// mando un messaggio
					//printf("faccio la send\n");
					sprintf(buf, "%d ", MSG_CONN_ACC);	
					//len= strlen(buf);// invio
					//ret = send(sock_com[first], (void*)buf, strlen(buf)+1, 0); //peer server che manda un messaggio al peer client sul primo socket libero
					ret = send_msg(sock_com[first], buf, "Errore in send sock_com[first]: "); 
						//peer server che manda un messaggio al peer client sul primo socket libero
					/*
					if(ret == -1)
					{
						if(errno != EINPROGRESS) 
						{
							perror("Errore in send sock_com[first]: ");
						}
					}	
					*/		
					//printf("%d\n", ret);
				}
				// else if(i==tcp_communicator1)
				else if(i==tcp_communicator1 || i==tcp_communicator2)
				{
					//char buf[BUFLEN];
					
					//ret = recv(tcp_communicator1, (void*)buf, BUFLEN, 0);
					//ret = recv(i, (void*)buf, BUFLEN, 0);
					ret = recv_msg(i, buf, "");
					sscanf(buf, "%d %[^\n]", (int*)&msgst.type, msgst.body);
					//sscanf(buf, "%hhu %[^\n]", &msgst.type, msgst.body); prova così
					if(ret == -1)
					{
						if(errno != EINPROGRESS) 
						{
						if(i==tcp_communicator1)
							perror("Errore in receive da communicator 1: ");
						else
							perror("Errore in receive ds communicator 2: ");
						}
					}			
					else if (ret==0) // chiusura del peer, lo tolgo dalla lista
					{
						if(i==tcp_communicator1)
							printf("\tConnessione chiusa in ricezione sul socket communicator 1 %d\n", tcp_communicator1);
						else
							printf("\tConnessione chiusa in ricezione sul socket communicator 2 %d\n", tcp_communicator1);
						// close(tcp_communicator1);
						// FD_CLR(tcp_communicator1, &master);
						close(i);
						FD_CLR(i, &master);
						//tcp_communicator1_enabled = false;
						//tcp_communicator1 = -1;
						if(i==tcp_communicator1)
						{
							tcp_communicator1_enabled = false;
							tcp_communicator1 = -1;
						}
						else
						{
							tcp_communicator2_enabled = false;
							tcp_communicator2 = -1;
						}
					}
					else if(msgst.type==MSG_CONN_CLOSE) // il client ha chiuso, chiudo il communicator
					{
						printf("\tc Registrata chiusura della connessione; chiudo tcp comm 1[%d]\n", tcp_communicator1);
						//close(tcp_communicator1);
						//FD_CLR(tcp_communicator1, &master);
						close(i);
						FD_CLR(i, &master);
						//tcp_communicator1_enabled = false;
						//tcp_communicator1 = -1;
						if(i==tcp_communicator1)
						{
							tcp_communicator1_enabled = false;
							tcp_communicator1 = -1;
						}
						else
						{
							tcp_communicator2_enabled = false;
							tcp_communicator2 = -1;
						}
						//break;
					}
					else if(msgst.type==MSG_CONN_ACC)
					{
						//tcp_communicator1_enabled=true;
						if(i==tcp_communicator1)
						{
							tcp_communicator1_enabled = true;
							printf("\tc La connessione è stata accettata dal peer server %d\n", neighbor[0]);
						}
						else
						{
							tcp_communicator2_enabled = true;
							printf("\tc La connessione è stata accettata dal peer server %d\n", neighbor[1]);
						}
						//ret = send(tcp_communicator1, (void*)buf, len, 0); //peer server che manda un messaggio al peer client sul primo socket libero
					}
					else if(msgst.type==MSG_REPLY_DATA)
					{
						pending_resp--; // il neighbor mi ha risposto
						// se in msgst.body c'è roba scrivi nel file dei totali ecc ecc ecc
						//if(strcmp(msgst.body, "")!=0 && pending_resp>=0)
						if(strcmp(msgst.body, "")!=0)
						//se il body non è vuoto vuol dire che il neighbor ha la data da me richiesta
						{
							//pending_resp=0; // la risposta è buona, non ne voglio altre per la data
							if(writeToFile(tot_log_name, msgst.body, pending_req)) // è effettivamente quella richiesta 
							{
								if(i==tcp_communicator1)
									printf("\tDato totale ricevuto dal neighbor %d\n", neighbor[0]); 
								else
									printf("\tDato totale ricevuto dal neighbor %d\n", neighbor[1]); 
								pending_req=getNextDay(pending_req); // chiedo il giorno successivo
								get(tcp_communicator1, tcp_communicator2, tcp_communicator1_enabled,
									tcp_communicator2_enabled, tot_log_name,
									pending_req, endPeriod, get_aggr, get_type, //(char)param1[0], (char)param2[0], 
									&pending_req, &pending_resp, startPeriod);  //richiamo la get con il giorno successivo nel range di richiesta
							}
						}
						// se ricevo NO_DATA prima di fare un FLOOD devo sapere se l'ho ricevuto da entrambi i neighbor (e sapere a chi dei due l'avevo mandato)
						else
						{
							char temp[10];
							
							if(pending_resp<=0 && pending_req!=0) // l'altro neighbor mi ha già risposto picche o non c'è, 
										// oppure ricevo dati vecchi e il totale è già stato calcolato
							{
								dateToStr(pending_req, temp);
								if(tcp_communicator1_enabled || tcp_communicator2_enabled)
								{
									printf("\tDato del totale mancante per il giorno %s;\n\t\trichiesto flood ai neighbor\n", temp);
									//sprintf(buf, "%d %d %d", MSG_FLOOD_FOR_ENTRIES, pending_req, peer_port); 
										//preparo buf convertendo in stringhe i numeri
										// invio anche la porta per evitare un loop nel flood (richieste a peer originatori di richieste)
								}
								else
									printf("\tDato del totale mancante per il giorno %s;\t\t\
										non ci sono neighbor disponibili per richiederlo\n", temp);
								if(tcp_communicator1_enabled)
								{	
									pending_resp++;
									sprintf(buf, "%d %d %d", MSG_FLOOD_FOR_ENTRIES, pending_req, peer_port); 
										//preparo buf convertendo in stringhe i numeri
										// invio anche la porta per evitare un loop nel flood (richieste a peer originatori di richieste)
									//ret=send(tcp_communicator1, (void*)buf, BUFLEN, 0); 				
									ret=send_msg(tcp_communicator1, buf, "Errore in send tcp_comm1: "); 				
									//ret=send(tcp_communicator1, (void*)buf, strlen(buf)+1, 0);
									/* 
									if(ret == -1)
									{
										if(errno != EINPROGRESS) 
										{
											perror("Errore in send tcp_comm1: ");
										}
									}
									*/
								}			
								if(tcp_communicator2_enabled)
								{
									pending_resp++;				
									sprintf(buf, "%d %d %d", MSG_FLOOD_FOR_ENTRIES, pending_req, peer_port); 
										//preparo buf convertendo in stringhe i numeri
										// invio anche la porta per evitare un loop nel flood (richieste a peer originatori di richieste)
									//ret=send(tcp_communicator1, (void*)buf, BUFLEN, 0); 
									ret=send_msg(tcp_communicator2, buf, "Errore in send tcp_comm2: "); 				
									//ret=send(tcp_communicator2, (void*)buf, strlen(buf)+1, 0); 
									/*
									if(ret == -1)
									{
										if(errno != EINPROGRESS) 
										{
											perror("Errore in send tcp_comm2: ");
										}
									}	
									*/		
								}
							}
						}
					}
					else if(msgst.type==MSG_RESP_FLOOD_FOR_ENTRIES) //devo controllare che la catena a ritroso sia finita e quindi se sono io che
											//sto richiedendo il dato mancante
					{
						char tempPort[8];
						int ret_date;
						uint16_t resp_port;
						int check_sock;
						char chain[BUFLEN];
						
						sscanf(msgst.body, "%d %d %[^\n]", (int*)&resp_port, &ret_date, buf); // metto in buf il resto del messaggio dopo la data richiesta 
						sscanf(buf, "%s", tempPort); 
						check_sock = sockForResp(tempPort); // socket per la risposta; 0 altrimenti; dopo in tempPort c'è la porta senza socket
						if(check_sock==0) // sono io che ho richiesto il dato
						{
							//pending_resp--;
							if(ret_date==pending_req) // la data è quella richiesta? (potrebbero arrivare risposte per vecchie date)
							{
								pending_resp--;
								if(resp_port==0 && pending_resp==0) // tutti mi hanno già risposto; nothin' doin' :(
								{
									char temp[10];

									dateToStr(pending_req, temp);
									printf("\tNon sono arrivati dati disponibili dai peers per il giorno %s;\
										\n\t\trichiesta get finora non onorabile\n", temp);
									pending_req = 0;
									//badLuck = false; // perché a volte arrivano 2 risposte no data sullo stesso comm?
									continue;
								}
							 	if (resp_port!=0) // il dato è buono?
								{
									//pending_resp=0; // ho la risposta per la data
									pending_for_ask=pending_req; // mi salvo la data per la send quando accetta la connessione
									pending_req=0; // così se arrivano altri dati li butto
									if(pending_resp<0)
										printf("\tHo ricevuto (in ritardo) il peer che ha il dato (%d): chiedo la connessione\n", resp_port);
									else
										printf("\tHo ricevuto il peer che ha il dato (%d): chiedo la connessione\n", resp_port);
									tcp_flooder=socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0); // socket per i dati flood
									flooder_addr.sin_port = htons(resp_port); // faccio una connect a chi ha il dato
									ret=connect(tcp_flooder, (struct sockaddr*)&flooder_addr, sizeof(flooder_addr));
									FD_SET(tcp_flooder, &master);
									if(fdmax<tcp_flooder)
										fdmax=tcp_flooder;
								}
								//else // nothin' doin' :(
								//	badLuck = true;
							}
							/*
							if(ret_date==pending_req) // la data è quella richiesta (potrebbero arrivare risposte per vecchie date)
							{
								if(!alreadyFLOODed) //ho già ricevuto la risposta dall'altro neighbor?
								{
									alreadyFLOODed = true; // così se ricevo la risposta anche dall'altro neighbor, la butto
								 	if (resp_port!=0) // il dato è buono?
									{
										printf("\tHo ricevuto il peer che ha il dato (%d): chiedo la connessione\n", resp_port);
										flooder_addr.sin_port = htons(resp_port); // faccio una connect a chi ha il dato
										ret=connect(tcp_flooder, (struct sockaddr*)&flooder_addr, sizeof(flooder_addr));
										FD_SET(tcp_flooder, &master);
										if(fdmax<tcp_flooder)
											fdmax=tcp_flooder;
									}
									else // nothin' doin' :(
										badLuck = true;
								}
								else if(resp_port==0 && badLuck) // nothin' doin' :(
								{
									char temp[10];

									dateToStr(pending_req, temp);
									printf("Non ci sono dati disponibili dai peers per il giorno %s\n", temp);
									pending_req = 0;
									badLuck = false; // perché a volte arrivano 2 risposte no data sullo stesso comm?

								}
							}
							*/
						}
						else
						{
							// il dato non è mio, lo inoltro indietro levando me stesso dalla lista dei peer contattati
							sscanf(msgst.body, "%*d %*d %*s %[^\n]", chain); // tolgo il dato della risposta, la data e me stesso dalla lista
							/* riutilizzo il codice di sockForResp solo per avere il peer giusto nella stampa */
							sscanf(chain, "%s", tempPort);
							ret = sockForResp(tempPort);
							/* */
							printf("\tInoltro all'indietro (%s) un flood non richiesto da me\n", tempPort);
							sprintf(buf, "%d %d %d %s", MSG_RESP_FLOOD_FOR_ENTRIES, resp_port, ret_date, chain); // ricostruisco il messaggio
							//ret = send(check_sock, (void*)buf, strlen(buf)+1, 0); // lo inoltro sul socket dal quale lo avevo ricevuto
							ret = send_msg(check_sock, buf, "Errore in send check_sock: "); // lo inoltro sul socket dal quale lo avevo ricevuto
							/*
							if(ret == -1)
							{
								if(errno != EINPROGRESS) 
								{
									perror("Errore in send check_sock: ");
								}
							}	
							*/		
						}						
					}						
				}
				/*
				else if(i==tcp_communicator2)
				{
					//char buf[BUFLEN];
					
					//printf("passa da tcp_communicator2\n");
					ret = recv(tcp_communicator2, (void*)buf, BUFLEN, 0);
					sscanf(buf, "%d %[^\n]", &msgst.type, msgst.body);
					//printf("%d %s - da communicator2\n", ret, buf);
					if(ret == -1)
					{
						if(errno != EINPROGRESS) 
						{
							perror("Errore in send check_sock: ");
						}
					}			
					/ *
					if (ret==0) // chiusura del peer, lo tolgo dalla lista
					{
						printf("\tConnessione chiusa in ricezione sul socket comm2 %d\n", tcp_communicator2);
						close(tcp_communicator2);
						FD_CLR(tcp_communicator2, &master);
						tcp_communicator2_enabled = false;
						tcp_communicator2 = -1;
					}
					else if (ret==-1) // errore in ricezione, lo tolgo dalla lista
					{
						printf("\tErrore in ricezione sul socket comm2 %d: chiudo connessione\n", tcp_communicator2);
						close(tcp_communicator2);
						FD_CLR(tcp_communicator1, &master);
						tcp_communicator2_enabled = false;
						tcp_communicator2 = -1;
					}
					* /
					if(msgst.type==MSG_CONN_CLOSE) // il client ha chiuso, chiudo il communicator
					{
						printf("\tc Registrata chiusura della connessione; chiudo tcp comm 2[%d]\n", tcp_communicator2);
						close(tcp_communicator2);
						FD_CLR(tcp_communicator2, &master);
						tcp_communicator2_enabled = false;
						tcp_communicator2 = -1;
						//break;
					}
					else if(msgst.type==MSG_CONN_ACC)
					{	
						printf("\tc La connessione è stata accettata dal peer server %d\n", neighbor[1]);
						tcp_communicator2_enabled=true;
					}
					else if(msgst.type==MSG_REPLY_DATA)
					{
						pending_resp--; // il neighbor mi ha risposto
						// se in msgst.body c'è roba scrivi nel file dei totali ecc ecc ecc
						if(strcmp(msgst.body, "")!=0 && pending_resp>=0)
						//se il body non è vuoto vuol dire che il neighbor ha la data da me richiesta, se pending_resp = -1 ha già risposto l'altro
						{
							pending_resp = 0; // metto a zero così il prossimo lo trova a -1
							writeToFile(tot_log_name, msgst.body, pending_req); 
							printf("\tDato totale ricevuto dal neighbor %d\n", neighbor[1]); 
							get(tcp_communicator1, tcp_communicator2, tcp_communicator1_enabled,
								tcp_communicator2_enabled, tot_log_name,
								getNextDay(pending_req), endPeriod, get_aggr, get_type, //(char)param1[0], (char)param2[0], 
								&pending_req, &pending_resp, startPeriod);  //richiamo la get con il giorno successivo nel range di richiesta
						}
						// se ricevo NO_DATA prima di fare un FLOOD devo sapere se l'ho ricevuto da entrambi i neighbor (e sapere a chi dei due l'avevo mandato)
						else
						{
							char temp[10];
							if(pending_resp==0) // l'altro neighbor mi ha già risposto picche o non c'è
							{
								sprintf(buf, "%d %d %d", MSG_FLOOD_FOR_ENTRIES, pending_req, peer_port); //preparo buf convertendo in stringhe i numeri
								// invio anche la porta per evitare un loop nel flood (richieste a peer originatori di richieste)
								if(tcp_communicator1_enabled)
								{
									ret=send(tcp_communicator1, (void*)buf, strlen(buf)+1, 0); 
									if(ret == -1)
									{
										if(errno != EINPROGRESS) 
										{
											perror("Errore in send sock1: ");
										}
									}			
								}
								if(tcp_communicator2_enabled)
								{	
									ret=send(tcp_communicator2, (void*)buf, strlen(buf)+1, 0); 
									if(ret == -1)
									{
										if(errno != EINPROGRESS) 
										{
											perror("Errore in send sock1: ");
										}
									}
								}			
								dateToStr(pending_req, temp);
								if(tcp_communicator1_enabled || tcp_communicator2_enabled)
								{
									if(tcp_communicator1_enabled)
										pending_resp++;;				
									if(tcp_communicator2_enabled)
										pending_resp++;;				
									printf("\tDato del totale mancante per il giorno %s;\n\t\trichiesto flood ai neighbor\n", temp);
								}
								else
									printf("\tDato del totale mancante per il giorno %s;\n\t\tnon ci sono neighbor disponibili\n", temp);
							}
						}
					}
					else if(msgst.type==MSG_RESP_FLOOD_FOR_ENTRIES)
					{
						char tempPort[8];
						int ret_date;
						uint16_t resp_port;
						int check_sock;
						char chain[BUFLEN];
						
						sscanf(msgst.body, "%d %d %[^\n]", &resp_port, &ret_date, buf); // metto in buf il resto del messaggio dopo la data richiesta 
						sscanf(buf, "%s", tempPort); 
						check_sock = sockForResp(tempPort); // socket per la risposta; 0 altrimenti; in tempPort dopo c'è la prta senza socket
						if(check_sock==0 ) // sono io che ho richiesto il dato
						{
							if(ret_date==pending_req) // la data è quella richiesta? (potrebbero arrivare risposte per vecchie date)
							{
								if(resp_port==0 && badLuck) // nothin' doin' :(
								{
									char temp[10];

									dateToStr(pending_req, temp);
									printf("Non ci sono dati disponibili dai peers per il giorno %s\n", temp);
									pending_req = 0;
									badLuck = false; // perché a volte arrivano 2 risposte no data sullo stesso comm?
									continue;
								}
							 	if (resp_port!=0) // il dato è buono?
								{
									pending_for_ask=pending_req; // mi salvo la data per la send quando accetta la connessione
									pending_req=0; // così se arrivano altri dati li butto
									printf("\tHo ricevuto il peer che ha il dato (%d): chiedo la connessione\n", resp_port);
									flooder_addr.sin_port = htons(resp_port); // faccio una connect a chi ha il dato
									ret=connect(tcp_flooder, (struct sockaddr*)&flooder_addr, sizeof(flooder_addr));
									FD_SET(tcp_flooder, &master);
									if(fdmax<tcp_flooder)
										fdmax=tcp_flooder;
								}
								else // nothin' doin' :(
									badLuck = true;
							}
						}
						else
						{
							// il dato non è mio, lo inoltro indietro levando me stesso dalla lista dei peer contattati
							sscanf(msgst.body, "%*d %*d %*s %[^\n]", chain); // tolgo il dato della risposta, la data e me stesso dalla lista
							/ * riutilizzo il codice di sockForResp solo per avere il peer giusto nella stampa * /
							sscanf(chain, "%s", tempPort);
							ret = sockForResp(tempPort);
							/ * * / 
							printf("\tInoltro all'indietro (%s) un flood non richiesto da me\n", tempPort);
							sprintf(buf, "%d %d %d %s", MSG_RESP_FLOOD_FOR_ENTRIES, resp_port, ret_date, chain); // ricostruisco il messaggio
							ret = send(check_sock, (void*)buf, strlen(buf)+1, 0); // lo inoltro sul socket dal quale lo avevo ricevuto
							if(ret == -1)
							{
								if(errno != EINPROGRESS) 
								{
									perror("Errore in send sock1: ");
								}
							}			
						}						
					}						
				}
				*/
				else if(i==tcp_flooder)
				{
					//char buf[BUFLEN];
					
					//ret = recv(tcp_flooder, (void*)buf, BUFLEN, 0);
					ret = recv_msg(tcp_flooder, buf, "Errore in receive da flooder: ");
					sscanf(buf, "%d %[^\n]", &msg_type, buf);
					/*
					if(ret == -1)
					{
						if(errno != EINPROGRESS) 
						{
							perror("Errore in receive da flooder: ");
						}
					}
					*/			
					//else if (ret==0) // chiusura del peer, lo tolgo dalla lista
					if (ret==0) // chiusura del peer, lo tolgo dalla lista
					{
						printf("\tConnessione chiusa in ricezione sul socket flooder %d\n", tcp_communicator1);
						close(tcp_flooder);
						FD_CLR(tcp_flooder, &master);
						tcp_flooder = -1;
					}
					else if(msg_type==MSG_CONN_ACC)
					{
						printf("\tLa connessione è stata accettata dal flooder\n");
						// devo fare la richiesta al peer che ha il dato quando ricevo l'accept
						printf("\tChiedo il dato al flooder\n");
						sprintf(buf, "%d %d", MSG_REQ_ENTRIES, pending_for_ask); //preparo buf convertendo in stringhe i numeri
						//ret=send(tcp_flooder, (void*)buf, strlen(buf)+1, 0); 
						ret=send_msg(tcp_flooder, buf, "Errore in send flooder: "); 
						/*
						if(ret == -1)
						{
							if(errno != EINPROGRESS) 
							{
								perror("Errore in send sock1: ");
							}
						}
						*/			
					}
					else if(msg_type==MSG_REPLY_ENTRIES)
					{
						char temp[10];
						int date;
						
						printf("\tDato totale ricevuto via flooding sul socket %d\n", tcp_flooder); 
						sscanf(buf, "%[^,]", temp); //recupero la data dell'ultima richiesta per la nuova richiesta
						date = atoi(temp);
						// scrivo il dato nel file e chiudo la connessione, e levo da &master
						writeToFile(tot_log_name, buf, pending_for_ask);
						close(tcp_flooder);
						FD_CLR(tcp_flooder, &master); // interrompo il canale virtuale con il mio flooder
						tcp_flooder = -1;
						//badLuck = false;
						get(tcp_communicator1, tcp_communicator2, tcp_communicator1_enabled,
							tcp_communicator2_enabled, tot_log_name,
							getNextDay(date), endPeriod, get_aggr, get_type, // (char)param1[0], (char)param2[0], 
							&pending_req, &pending_resp, startPeriod);  //richiamo la get con il giorno successivo nel range di richiesta
					}
				}
				else if(i==sd) //comunicazione UDP con il ds
				{
					//char buf[BUFLEN];
					/*
					do
					{
						len=recvfrom(sd, buf, BUFLEN, 0, (struct sockaddr*)&sv_addr, &addrlen); 
					}while(len<0);
					*/
					len=recvfrom(sd, buf, BUFLEN, 0, (struct sockaddr*)&sv_addr, &addrlen); 
					if (len!=BUFLEN) // risposta ricevuta 
						perror("Errore in receivefrom: ");
					else
					{
						recvport = ntohs(sv_addr.sin_port);
						if(recvport==srvport) // controllo di ricevere dati dal server e non da un peer
						{
							/*msg_type = (checkInput(buf)); // controllo la correttezza della lunghezza
							if(msg_type == 0)
							{
								perror("Errore in receivefrom: ");
								return 0;
							}*/
							sscanf(buf, "%d", &msg_type);
							if(msg_type==MSG_PEERLIST) // lista dei neighbor
							{
								uint16_t temp[2];
								
								temp[0]=neighbor[0]; // mi salvo i vecchi neighbor; se sono gli stessi non disconnetto e riconnetto
								temp[1]=neighbor[1];
								sscanf(buf, "%d %d %d", &msg_type, (int*)&neighbor[0], (int*)&neighbor[1]);
								// chiudo le precedenti connessioni se i neghibors erano !=0
								printf("\tc Chiudo precedenti connessioni coi neighbors: %d %d\n\t\t(se stabilite e se necessario)\
										\n", temp[0], temp[1]);
								//sprintf(temp_msg, "%d ", MSG_CONN_CLOSE); // invio messaggio agli eventuali peer connessi che chiudo	
								if(temp[0]!=0 && tcp_communicator1_enabled && neighbor[0]!=temp[0]) 
								{
									sprintf(temp_msg, "%d ", MSG_CONN_CLOSE); // invio messaggio agli eventuali peer connessi che chiudo	
									//ret = send(tcp_communicator1, (void*)temp_msg, strlen(temp_msg)+1, 0);
									ret = send_msg(tcp_communicator1, temp_msg, "Errore in send tcp_1: ");
									/*
									if(ret == -1)
									{
										if(errno != EINPROGRESS) 
										{
											perror("Errore in send sock1: ");
										}
									}
									*/			
									/*
									if(ret == -1)
									{
										if(errno != EINPROGRESS) 
										{
											perror("Errore in send tcp_1: ");
										}
									}
									*/
									close(tcp_communicator1);
									FD_CLR(tcp_communicator1, &master); // interrompo il canale virtuale con il mio neighbor1
									tcp_communicator1_enabled=false;
									tcp_communicator1 = -1;
								}
								if(temp[1]!=0 && tcp_communicator2_enabled && neighbor[1]!=temp[1]) 
								{
									sprintf(temp_msg, "%d ", MSG_CONN_CLOSE); // invio messaggio agli eventuali peer connessi che chiudo	
									//ret = send(tcp_communicator2, (void*)temp_msg, strlen(temp_msg)+1, 0);
									ret = send_msg(tcp_communicator2, temp_msg, "Errore in send tcp_2: ");
									/*
									if(ret == -1)
									{
										if(errno != EINPROGRESS) 
										{
											perror("Errore in send tcp_2: ");
										}
									}
									*/
									close(tcp_communicator2);
									FD_CLR(tcp_communicator2, &master); // interrompo il canale virtuale con il mio neighbor2
									tcp_communicator2_enabled=false;
									tcp_communicator2 = -1;
								}
								printf("\tAggiornata lista dei neighbors: %d %d\n", neighbor[0], neighbor[1]);
								//se i neighbor esistono stabilisco una connessione tcp con essi
								if(neighbor[0]!=0) 
								{
									if(neighbor[0]!=temp[0])
									{
										tcp_communicator1=socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0); 
										tcp_communicator1_enabled = true;
										neigh_addr.sin_port=htons(neighbor[0]);
										//tcp_communicator1_enabled = true; // abilito il communicator
										FD_SET(tcp_communicator1, &master);
										if(fdmax<tcp_communicator1)
											fdmax=tcp_communicator1;
										printf("\tc Mi connetto a %d su tcp comm 1[%d]\n", neighbor[0], tcp_communicator1);
										ret=connect(tcp_communicator1, (struct sockaddr*)&neigh_addr, sizeof(neigh_addr));
										if(ret == -1)
										{
											if(errno != EINPROGRESS) 
											{
												perror("Errore in connect tcp_1: ");
											}
										}
									}
									else
										printf("\tc Resto connesso a a %d su tcp comm 1[%d]\n", neighbor[0], tcp_communicator1);
								}
								if(neighbor[1]!=0) 
								{
									if(neighbor[1]!=temp[1])
									{
										tcp_communicator2=socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0); 
										tcp_communicator2_enabled = true;
										neigh_addr.sin_port=htons(neighbor[1]);
										//tcp_communicator2_enabled = true; // abilito il communicator
										FD_SET(tcp_communicator2, &master);
										if(fdmax<tcp_communicator2)
											fdmax=tcp_communicator2;
										printf("\tc Mi connetto a %d su tcp comm 2[%d]\n", neighbor[1], tcp_communicator2);
										ret=connect(tcp_communicator2, (struct sockaddr*)&neigh_addr, sizeof(neigh_addr));
										if(ret == -1)
										{
											if(errno != EINPROGRESS) 
											{
												perror("Errore in conncect tcp_2: ");
											}
										}
									}
									else
										printf("\tc Resto connesso a a %d su tcp comm 2[%d]\n", neighbor[1], tcp_communicator2);
								}								
							}
							// la prossima non gestita lato server perché non indicate le specifiche
							if(msg_type==MSG_CLOSE_REQ) // richiesta di chiusura dal server
							{
								closed = 1;
								time_t rawtime=time(NULL);
								ptm = localtime(&rawtime);
								strftime(curr_date_time, sizeof(curr_date_time), "%Y%m%d", ptm);
								// mi salvo la data perché la chiusura è stata richiesta per questa							
								closed_date = atoi(curr_date_time); // converto in numerico per gestirlo meglio
								printf("\tChiusura richiesta dal server\n");
								if (!calculated) // se non ancora calcolato il totale giornaliero, calcolo
								{
									char temp[10];
									dateToStr(closed_date, tempStr);
									printf("\tCalcolo i totali del giorno %s\n", tempStr);
									rawtime += 3600*24; // vado al giorno dopo
									ptm = localtime(&rawtime);
									strftime(temp, sizeof(temp), "%Y%m%d", ptm);
									calculate(log_name, tot_log_name, atoi(temp));
									calculated = 1;
								}
							}
						}
					}
				}
				else if(i == STDIN)
				{
					fgets(line, 150, stdin);
					num=sscanf(line, "%s %s %s %s", cmd, param1, param2, param3);
					if(num==3 && strcmp(cmd, "start") == 0) 
					{
						srvport = atoi(param2);
						inet_pton(AF_INET, param1, &srvaddr);
						if (boot(sd, param1, param2, peer_port, neighbor))
						{
							printf("\tRicevuta lista dei neighbors: %d %d\n", neighbor[0], neighbor[1]);
							//se i neighbor esistono stabilisco una connessione tcp con essi
							if(neighbor[0]!=0) 
							{
								neigh_addr.sin_port=htons(neighbor[0]);
								//tcp_communicator1_enabled = true; // abilito il communicator
								FD_SET(tcp_communicator1, &master);
								if(fdmax<tcp_communicator1)
									fdmax=tcp_communicator1;
								printf("\tc Mi connetto a %d\n", neighbor[0]);
								ret=connect(tcp_communicator1, (struct sockaddr*)&neigh_addr, sizeof(neigh_addr));
								if(ret == -1)
								{
									if(errno != EINPROGRESS) 
									{
										perror("Errore in connect tcp_1: ");
									}
								}
							}
							if(neighbor[1]!=0)
							{
								neigh_addr.sin_port=htons(neighbor[1]);
								//tcp_communicator2_enabled = true; // abilito il communicator
								FD_SET(tcp_communicator2, &master);
								if(fdmax<tcp_communicator2)
									fdmax=tcp_communicator2;
								printf("\tc Mi connetto a %d\n", neighbor[1]);
								ret=connect(tcp_communicator2, (struct sockaddr*)&neigh_addr, sizeof(neigh_addr));
								if(ret == -1)
								{
									if(errno != EINPROGRESS) 
									{
										perror("Errore in connect tcp_2: ");
									}
								}
							}
						}
					}
					else if(num==3 && strcmp(cmd, "add") == 0) 
					{
						quantity=atoi(param2);
						//if((!strcmp(param1, "tampone") && !strcmp(param1, "nuovo caso")) || quantity<=0)
						if((strcmp(param1, "T") && strcmp(param1, "N")) || quantity<=0) // SI CONSIDERANO ANCHE < 0 DIMINUZIONI ? nel caso quantity==0
							printf("Sintassi del comando errata: <type> deve essere N[uovo caso] o T[ampone],\n\t<quantity> maggiore di 0\n");
						else
							calculated = add(param1[0], quantity, closed, log_name, tot_log_name);
					}
					else if(num==4 && strcmp(cmd, "get") == 0)
					{
						if(strcmp(param1, "T") && strcmp(param1, "V"))
							printf("Sintassi del comando errata: <aggr> deve essere T[otale] o V[ariazione]\n");
						else if(strcmp(param2, "T") && strcmp(param2, "N"))
							printf("Sintassi del comando errata: <type> deve essere N[uovo caso] o T[ampone]\n");
						/*
						else if(srvport==0)
							printf("\tRegistazione non ancora effettuata. Utilizzare prima il comando start\n");
						else if(neighbor[0]==0 && neighbor[1]==0)
							printf("\tNon ci sono peer disponibili per la richiesta\n");
						*/
						else if(!checkPeriod(param3, &startPeriod, &endPeriod, closed))
						{
							printf("Sintassi del comando errata: <period> deve essere nel formato\n");
							printf("\tdd:mm:yyyy|*-dd:mm:yyyy|* ne la data iniziale non deve\n");
							printf("\tessere minore della data finale\n\te la data finale non dev'essere successiva all'ultima chiusura\n");
						}
						else
						{
							/* rifaccio il calcolo perché potrebbe essere scatta la chiusura oraria */
							time_t rawtime=time(NULL);
							ptm = localtime(&rawtime);
							strftime(curr_date_time, sizeof(curr_date_time), "%H", ptm);
							hr = atoi(curr_date_time); 
							strftime(curr_date_time, sizeof(curr_date_time), "%Y%m%d", ptm);
							if (hr>17 && closed==0) // chiusura per orario
							{
								closed = 1;
								closed_date = atoi(curr_date_time); // converto in numerico per gestirlo meglio
								printf("\tChiusura per raggiunto limite orario\n");
								if (!calculated) // se non ancora calcolato il totale giornaliero, calcolo
								{
									char temp[10];
									dateToStr(closed_date, tempStr);
									printf("\tCalcolo i totali del giorno %s\n", tempStr);
									rawtime += 3600*24; // vado al giorno dopo
									ptm = localtime(&rawtime);
									strftime(temp, sizeof(temp), "%Y%m%d", ptm);
									calculate(log_name, tot_log_name, atoi(temp));
									calculated = 1;
								}
							}
							/* */
							if(pending_req!=0)
								printf("\tPrecedente richiesta get abortita\n");
							pending_req = 0;
							//alreadyFLOODed=false;
							//badLuck = false;
							get_aggr=(char)param1[0];
							get_type=(char)param2[0];
							get(tcp_communicator1, tcp_communicator2, tcp_communicator1_enabled,
								tcp_communicator2_enabled, tot_log_name,
								startPeriod, endPeriod, get_aggr, get_type, // (char)param1[0], (char)param2[0], 
								&pending_req, &pending_resp, startPeriod); 
								// pending: req = il dato, resp 0|1|2 la risposte attese dai neighbor					
						}
					}	
					else if(num==1 && strcmp(cmd, "stop") == 0) 
					{
						int s;
						// invio tutte le registrazioni TODO ????
						//sprintf(buf, "%d ", MSG_CONN_CLOSE); // invio messaggio agli eventuali peer connessi che chiudo	
						//len = strlen(buf);
						//ret = send(tcp_client, (void*)buf, len, 0);						
						//close(tcp_client);
						for(s=0; s<17; s++)
						{
							if(sock_com[s]==-1)
								continue;							
							printf("\tInvio chiusura a sock %d[%d]\n", sock_com[s], s);
							sprintf(buf, "%d ", MSG_CONN_CLOSE); // invio messaggio agli eventuali peer connessi che chiudo	
							//ret = send(sock_com[s], (void*)buf, len, 0);						
							ret = send_msg(sock_com[s], buf, "Errore in send sock: ");
							/*						
							if(ret == -1)
							{
								if(errno != EINPROGRESS) 
								{
									perror("Errore in send sock: ");
								}
							}
							*/
							close(sock_com[s]);
							FD_CLR(sock_com[s], &master); // nel caso avessi fatto una connect rimasta appesa
							sock_com[s] = -1;
						}
						if(tcp_communicator1_enabled)
						{
							printf("\tChiudo neighbor comm 1\n");
							sprintf(buf, "%d ", MSG_CONN_CLOSE); // invio messaggio agli eventuali peer connessi che chiudo	
							//ret = send(tcp_communicator1, (void*)buf, len, 0);						
							ret = send_msg(tcp_communicator1, buf, "Errore in send tcp_1: ");
							/*						
							if(ret == -1)
							{
								if(errno != EINPROGRESS) 
								{
									perror("Errore in send tcp_1: ");
								}
							}
							*/
							close(tcp_communicator1);
							FD_CLR(tcp_communicator1, &master); // interrompo il canale virtuale con il mio neighbor1
							tcp_communicator1_enabled = false;
							tcp_communicator1 = -1;
							
						}
						if(tcp_communicator2_enabled)
						{
							printf("\tChiudo neighbor comm 2\n");
							sprintf(buf, "%d ", MSG_CONN_CLOSE); // invio messaggio agli eventuali peer connessi che chiudo	
							//ret = send(tcp_communicator2, (void*)buf, len, 0);						
							ret = send_msg(tcp_communicator2, buf, "Errore in send tcp_2: ");
							/*
							if(ret == -1)
							{
								if(errno != EINPROGRESS) 
								{
									perror("Errore in send tcp_2: ");
								}
							}
							*/
							close(tcp_communicator2);
							FD_CLR(tcp_communicator2, &master); // interrompo il canale virtuale con il mio neighbor2
							tcp_communicator2_enabled = false;
							tcp_communicator2 = -1;
						}
						if(srvport!=0) // mi sono già registrato in precedenza
							deReg(sd, srvaddr, srvport, peer_port, neighbor); // dico al server che chiudo
						close(tcp_listener);
						exit(0);
					}
					else
						printf("Comando non riconosciuto (comandi validi: start | add | get | stop)\n\n");
				}
				else // comportamento da peer server
				{

					int k;
					for(k=0; k<17; k++)
					{
						if(i==sock_com[k])
							break;
					}
					if(k==17) // non ce n'è punti
						break;
					printf("\ts Ricevo sul socket %d[%d]\n", sock_com[k], k);
					// ret = recv(sock_com[k], (void*)buf, BUFLEN, 0);
					ret = recv_msg(sock_com[k], buf, "Errore in receive sul socket: ");
					sscanf(buf, "%d %[^\n]", (int*)&msgst.type, msgst.body);
					/*
					if(ret == -1)
					{
						if(errno != EINPROGRESS) 
						{
							perror("Errore in receive sul socket: ");
						}
					}		
					*/	
					//else if (ret==0) // chiusura del peer, lo tolgo dalla lista
					if (ret==0) // chiusura del peer, lo tolgo dalla lista
					{
						printf("\tConnessione chiusa in ricezione sul socket #%d\n", sock_com[k]);
						close(sock_com[k]);
						FD_CLR(sock_com[k], &master);
						sock_com[k] = -1;
					}
					else if(msgst.type==MSG_CONN_CLOSE) // il client ha chiuso, chiudo il sock_com
					{
						printf("\ts Registrata chiusura della connessione socket #%d\n", k);
						close(sock_com[k]);
						FD_CLR(sock_com[k], &master);
						sock_com[k] = -1;
					}
					else if(msgst.type==MSG_REQ_DATA || msgst.type==MSG_REQ_ENTRIES) //un mio peer client mi ha chiesto un dato, oppure un floodee
					{
						sscanf(msgst.body, "%d", &req_data);
						dateToStr(req_data, req_data_date);
						printf("\tRicevuta richiesta dati per il giorno %s\n", req_data_date);
						getData(req_data, req_data_data, tot_log_name); //se il peer server ha la data che il peer client richiede la mette
											//in req_data_data sottoforma di stringa, altrimenti ci mette una stringa vuota
						if(strcmp(req_data_data, "")==0)
							printf("\tNon ho i dati...\n");
						//printf("%s\n", req_data_data); //è giusto
						if(msgst.type==MSG_REQ_DATA)
							sprintf(buf, "%d %s", MSG_REPLY_DATA, req_data_data); //preparo il buf
						else
							sprintf(buf, "%d %s", MSG_REPLY_ENTRIES, req_data_data); //preparo il buf
						//ret=send(sock_com[k], (void*)buf, strlen(buf)+1, 0); // invia il dato; se non l'ho invio tipo messaggio e una stringa vuota		
						ret=send_msg(sock_com[k], buf, "Errore in send sock: "); 
							// invia il dato; se non ce l'ho invio tipo messaggio e una stringa vuota		
						/*
						if(ret == -1)
						{
							if(errno != EINPROGRESS) 
							{
								perror("Errore in send sock1: ");
							}
						}
						*/			
					}
					else if(msgst.type==MSG_FLOOD_FOR_ENTRIES) // un peer (client) che ha me come neighbor mi ha chiesto un flood
					{
						char tempPort[8];
						char chain[BUFLEN];
						int check_l;
						sscanf(msgst.body, "%d %[^\n]", &req_data, chain); // metto in chain il resto del messaggio dopo la data richiesta (quindi una lista di 
												//formato n_porta:id_socket separata da spazi)
						dateToStr(req_data, req_data_date);
						printf("\tRicevuta richiesta flood per il giorno %s\n", req_data_date);
						sscanf(chain, "%s", tempPort); // guardo se la richiesta non è da un peer che mi ha come neighbor
										// (nel caso ha già fatto un REQ_DATA e non ho il dato)--> è inutile invocare la getData
						if(sockForResp(tempPort)!=0) // in tempPort mette la porta senza socket
						{
							// controlla i tuoi dati ed eventualmente rispondi
							getData(req_data, req_data_data, tot_log_name); //se il peer server ha la data che il peer client richiede
										// la mette in req_data_data sottoforma di stringa, altrimenti ci mette una stringa vuota
							if(strcmp(req_data_data, "")!=0) //se il peer ha la data richiesta 
							{
								//ho il dato, invio la mia porta e la data
								//char* temp;
								//temp=buf;
								//sprintf(buf, "%d %s %s", MSG_RESP_FLOOD_FOR_ENTRIES, peer_port, temp);
								printf("\tHo i dati per il giorno %s; li invio (%s)\n", req_data_date, tempPort);
								sprintf(buf, "%d %d %d %s", MSG_RESP_FLOOD_FOR_ENTRIES, peer_port, req_data, chain);
								//ret=send(sock_com[k], (void*)buf, strlen(buf)+1, 0);
								ret=send_msg(sock_com[k], buf, "Errore in send sock: ");
								/*
								if(ret == -1)
								{
									if(errno != EINPROGRESS) 
									{
										perror("Errore in send sock1: ");
									}
								}
								*/			
								break;
							}
						}
						check_l=checkLooping(buf, neighbor); // ritorna 0 se non c'è nessun neighbor nella lista, 
										// 1 se c'è il primo neighbor, 2 se c'è il secondo e 3 se ci sono entrambi
						// controlla se nella lista dei peer attraversati c'è già uno dei miei (al quale non rimando richiesta)
						if(check_l==3)
						{
							/* riutilizzo il codice di sockForResp solo per avere il peer giusto nella stampa */
							//sscanf(chain, "%s* %s", tempPort);
							sscanf(chain, "%s %s", tempPort, tempPort);
							ret = sockForResp(tempPort);
							/* */
							printf("\tI miei neighbors hanno già avuto richieste di flood per\n\t\tla data %s: blocco il flood; nessun dato;",
								req_data_date);
							printf("\n\t\tinvio la risposta (%s)\n", tempPort);
							sprintf(buf, "%d %d %d %s", MSG_RESP_FLOOD_FOR_ENTRIES, 0, req_data, chain); // è impossbile proseguire il FLOOD_FOR_ENTRIES 
															// perchè si entra in un loop 
							//ret=send(sock_com[k], (void*)buf, strlen(buf)+1, 0);							
							ret=send_msg(sock_com[k], buf, "Errore in send sock1: ");							
							/*
							if(ret == -1)
							{
								if(errno != EINPROGRESS) 
								{
									perror("Errore in send sock1: ");
								}
							}
							*/			
						}
						else
						{
							//sprintf(buf, "%d %d %d:%d %s", MSG_FLOOD_FOR_ENTRIES, req_data, peer_port, sock_com[k], chain);
							// preparo buf convertendo in stringhe i numeri
							// invio anche la porta per evitare un loop nel flood (richieste a peer originatori di richieste)
							// e il socket per sapere di chi era la richiesta
							printf("\tInoltro il flood (%s) a miei neighbors\n\t\t(se non hanno già avuto richieste per la data %s)\n", 
									tempPort, req_data_date);
							if(check_l==2 || check_l==0)
							{	
								sprintf(buf, "%d %d %d:%d %s", MSG_FLOOD_FOR_ENTRIES, req_data, peer_port, sock_com[k], chain);
								//ret=send(tcp_communicator1, (void*)buf, strlen(buf)+1, 0);
								ret=send_msg(tcp_communicator1, buf, "Errore in send sock1: ");
								/*
								if(ret == -1)
								{
									if(errno != EINPROGRESS) 
									{
										perror("Errore in send sock1: ");
									}
								}
								*/			
							}
							if(check_l==1 || check_l==0)
							{
								sprintf(buf, "%d %d %d:%d %s", MSG_FLOOD_FOR_ENTRIES, req_data, peer_port, sock_com[k], chain);
								//ret=send(tcp_communicator2, (void*)buf, strlen(buf)+1, 0);
								ret=send_msg(tcp_communicator2, buf, "Errore in send sock2: ");
								/*
								if(ret == -1)
								{
									if(errno != EINPROGRESS) 
									{
										perror("Errore in send sock2: ");
									}
								}
								*/			
							}
						}
					}
					else 
						printf("Messaggio sconosciuto: %s\n", buf);
				}
			}
		}
	}
}
