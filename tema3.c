#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


//returneaza vecinii din topologie ai unui nod
int getVecini(int rank, int size, int *v, int *top){
	int rez = 0;
	for (int i= 0; i<size; i++){
		if (top[rank * size + i] == 1){
			v[rez] = i;
			rez++;
		}
	}
	return rez;
}

//sau logic intre 2 topologii
void combineTop(int* t1, int*t2, int size){
	for (int i = 0; i<size*size; i++){
		t1[i] = t1[i] || t2[i];
	}
}

//aplica un filtru anume, pentru un pixel dintr-un bloc
int applyFilterForPixel(int filterTag, int*recv, int height, int width, int i, int j){
	int smoothVec[10] = {9,1,1,1,1,1,1,1,1,1};
	int blurVec[10] = {16,1,2,1,2,4,2,1,2,1};
	int sharpenVec[10] = {3,0,-2,0,-2,11,-2,0,-2,0};
	int meanVec[10] = {1,-1,-1,-1,-1,9,-1,-1,-1,-1};
	int filterVec[10];
	switch (filterTag) {
		case 3:
			memcpy(filterVec,smoothVec,10*sizeof(int));
			break;
		case 4: 
			memcpy(filterVec,blurVec,10*sizeof(int));
			break;
		case 5:
			memcpy(filterVec,sharpenVec,10*sizeof(int));
			break;
		case 6:
			memcpy(filterVec,meanVec,10*sizeof(int));
			break;
	}
	
	int rez = recv[(i-1)*width + j-1]*filterVec[1] +
		recv[(i-1)* width + j]*filterVec[2] +
		recv[(i-1)* width + j + 1]*filterVec[3] +
		recv[i* width + j - 1]*filterVec[4] +
		recv[i* width + j]*filterVec[5] +
		recv[i* width + j + 1]*filterVec[6] +
		recv[(i+1)* width + j - 1]*filterVec[7] +
		recv[(i+1)* width + j]*filterVec[8] +
		recv[(i+1)* width + j + 1]*filterVec[9];
	if (rez/filterVec[0] > 255){
		return 255;
	}
	if (rez/filterVec[0] < 0){
		return 0;
	}
	return (rez/filterVec[0]);
}

//aplica un filtru anume pentru un bloc de dimensiuni height * width
void applyFilter(int filterTag, int* recv, int height, int width, int* result){
	for (int i = 1; i<height - 1; i++){
		for (int j = 1; j<width - 1; j++){
			result[i* width + j] = applyFilterForPixel(filterTag,recv,height,width, i, j);
		}
	
	}
}


int main(int argc, char* argv[]){

	MPI_Init(&argc, &argv);
	int rank, size;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Status status;
	int top[size * size];
	for (int i = 0; i < size*size; i++){
		top[i] = 0;
	}
	int topRecv[size * size];
	FILE* top_file = fopen(argv[1], "r");
	int aux;
	
	//citire topolgie
	//nodul k sare peste k-1 linii si o citeste pe cea care ii corespunde
	
	char* line = malloc (2 * size);
	size_t length = 2 * size;

	int lineNo = 0;
	while (lineNo < rank) {
		getline(&line, &length, top_file);
		lineNo++;
	}
	
	fscanf(top_file,"%d:", &aux);
		
	while ( fscanf(top_file,"%d", &aux) > 0){
		top[rank * size + aux] += 1;
		top[aux * size + rank] += 1;
	}
	if (rank != size - 1){
		top[rank * size + aux] -= 1;
		top[aux * size + rank] -= 1;
	}

	// aflarea topologiei

	int *vecini = malloc (size * sizeof(int));
	int nrVecini = getVecini(rank, size, vecini, top);
	int parent = -1;
	int *copii = malloc (size * sizeof(int));
	int nrCopii = 0;

	if (rank == 0) {

		//sonda catre toti vecinii
		for (int i = 0; i<nrVecini; i++){
			MPI_Send(top, size * size, MPI_INT, vecini[i], 1,MPI_COMM_WORLD);
		}
		fflush(stdout);

		//raspuns de la fiecare din vecini
		for (int i = 0; i<nrVecini; i++){
			MPI_Recv(topRecv, size*size, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
			if (status.MPI_TAG == 2){	//tag-ul 2: nodul a identificat sender-ul ca parinte
				copii[nrCopii] = status.MPI_SOURCE;
				nrCopii++; //marchez fiul 
			} else {	//nodul nu este fiul senderului
				MPI_Send(top,16,MPI_INT,status.MPI_SOURCE,1,MPI_COMM_WORLD); //raspund vecinilor care nu sunt fii
			}
			combineTop(top, topRecv, size);
		}
	}
	if (rank!=0){

		//astept o sonda, cel de la care o primesc devine automat parinte;
		MPI_Recv(topRecv, size * size, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
		parent = status.MPI_SOURCE;

		fflush(stdout);

		for (int i = 0; i<nrVecini; i++){
			if (vecini[i] != parent){
				MPI_Send(top, size * size, MPI_INT, vecini[i], 1, MPI_COMM_WORLD);
				fflush(stdout);
				MPI_Recv(topRecv, size*size, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
				if (status.MPI_TAG == 1){ //daca primesc de la un vecin simplu, combin topologiile
					combineTop(top, topRecv, size);			
				} else {	//daca primesc de la un fiu, il adaug in vector si combin topologia cu a mea
					copii[nrCopii] = status.MPI_SOURCE;
					nrCopii++;			
					combineTop(top, topRecv, size);
				}
			}
		}

		//instiintez parintele ca sunt fiul lui;
		MPI_Send(top, size*size, MPI_INT, parent, 2, MPI_COMM_WORLD);
	}
	
	printf("%d:Parintele meu este %d.Copiii lui %d sunt:\n",rank,parent, rank);
	for (int i = 0; i<nrCopii; i++){
		printf("%d ", copii[i]);
	}
	printf("\n");
	fflush(stdout);

	//am aflat topologia. Urmeaza procesarea imaginilor

	int nrImg, width, height, maxGV, filterTag=1,finalTag = 10, processedLines = 0;
	FILE* img_file = fopen(argv[2], "r");
	if (rank == 0){
		//deschid fisierul de imagini
		fscanf(img_file, "%d", &nrImg);
		fgets(line, 10, img_file); //termin linia
		int *hw = (int*)calloc(2,sizeof(int));
		for (int i = 0; i<nrImg; i++) {
			fgets(line, 256, img_file);
			char *filter, *crtImg, *resImg;
			char*tok  = line;
			int k = 0;
    		
    		//citesc o linie din fisierul de imagini si o "sparg" in 3
    		while ((tok = strtok(tok, " ")) != NULL){
        		fflush(stdout);
        		k++;
				
				if (k == 1){
					filter = malloc ( strlen(tok) +1);
					strcpy(filter, tok);
				}
				if (k == 2){
					crtImg = malloc ( strlen(tok) +1);
					strcpy(crtImg, tok);				
				}				
				if (k == 3){
					if (tok[strlen(tok) -1] == '\n')
						tok[strlen(tok) - 1] = '\0';
					resImg = malloc ( strlen(tok) +1);
					strcpy(resImg, tok);
				}
				tok = NULL;
    		}

    		//aflu filtrul
			if (strcmp(filter, "smooth") == 0){
				filterTag = 3;
			}
			if (strcmp(filter, "blur") == 0){
				filterTag = 4;
			}
			if (strcmp(filter, "sharpen") == 0){
				filterTag = 5;
			}
			if (strcmp(filter, "mean_removal") == 0){
				filterTag = 6;
			}

			
			//deschid imaginea curenta
			FILE *crt_img = fopen (crtImg, "r");
			char* firstLine = malloc (10);
			char* secondLine = malloc (256);
			fgets(firstLine, 10, crt_img);
			fgets(secondLine, 256, crt_img);
			
			fscanf(crt_img, "%d", &width);
			fscanf(crt_img, "%d", &height);
			fscanf(crt_img, "%d", &maxGV);
			
			
			//pun imaginea intr-un bloc si o bordez cu 0
			int* image = calloc((height+2)*(width+2), sizeof(int));
			for (int j = 1; j<=height; j++){
				for (int k = 1; k<=width; k++){
					fscanf(crt_img, "%d", &(image[j*(width+2)+k]));
				}					
			}
			//bordare
			for (int j = 0; j<height + 2; j++){
				image[j*(width+2)] = 0;
				image[j*(width+2)+width+1] = 0;
			}
			for (int j = 0; j<width + 2; j++){
				image[j] = 0;
				image[(height+1)*(width+2)+j] = 0;
			}


			//Impartire linii din radacina
			if (nrCopii > 0){
				int nrCopiiActivi = nrCopii;
				int liniiCopil = height/nrCopii;
				int liniiSend;
				int rest = height % nrCopii;
				int crtLine = 1;
				int* firstLineOfChild = (int*)calloc(size , sizeof(int));
				for (int j = 0; j<nrCopii; j++){ //trimit blocul corespunzator fiecarui copil
					if ((rest > 0) && (j == nrCopii - 1)) {
						liniiSend = liniiCopil + rest;
					} else {
						liniiSend = liniiCopil;
					}

					hw[0] = liniiSend + 2;
					hw[1] = width+2;

					if (liniiSend > 0){
						
						//construire string de trimis
						
						firstLineOfChild[copii[j]] = crtLine -1;
						int *toSend = (int*)calloc((liniiSend+2)* (width+2),sizeof(int));
						
						
						for (int k = crtLine - 1; k <= crtLine+liniiSend; k++){
							for (int l = 0; l<width+2; l++){
								toSend[(k-crtLine+1)*(width+2)+l] = image[k*(width+2)+l];
							}
						}
						
						MPI_Send(hw, 2,MPI_INT, copii[j],1,MPI_COMM_WORLD);
						MPI_Send(toSend,(liniiSend+2)*(width+2),MPI_INT,copii[j],filterTag,MPI_COMM_WORLD);
						crtLine += liniiSend;
					} else { //daca pentru un copil nu raman linii, il anunt
						hw[0] = -1;
						MPI_Send(hw,2,MPI_INT,copii[j],1,MPI_COMM_WORLD);
						nrCopiiActivi--;
					}
				}


				//primesc si asamblez rezultatele
				int*result = (int *)calloc ((height+2)*(width+2),sizeof(int));
				int*recv;
				for (int j = 0; j<nrCopiiActivi; j++){
					MPI_Recv(hw,2,MPI_INT,MPI_ANY_SOURCE,MPI_ANY_TAG,MPI_COMM_WORLD, &status);
					recv = (int*)calloc (hw[0]*hw[1],sizeof(int));
					MPI_Recv(recv, hw[0]*hw[1], MPI_INT, status.MPI_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
					

					crtLine = firstLineOfChild[status.MPI_SOURCE] +1;
					//prima linie asignata fiecarui copil, de la ea incep sa copiez

					int l = width + 2;
					for (int k = crtLine*(width+2); k<(crtLine+hw[0] - 2)*hw[1]; k++){
						result[k] = recv[l];
						l++;
					}
					
				}		
				
				
				//afisare in fisier
				FILE* res_img = fopen (resImg, "w");
				fprintf(res_img, "%s", firstLine);
				fprintf(res_img, "%s", secondLine);
				fprintf(res_img, "%d %d\n%d\n", width,height,maxGV);
				for (int j = 1; j<=height; j++){
					for (int k = 1; k<=width; k++){
						fprintf(res_img, "%d\n", result[j*(width+2)+k]);
					}
				}
				fclose(res_img);
				
			}	else { // radacina e frunza
				

				//prelucrare cu filtre
				int*result = (int *)calloc ((height+2)*(width+2),sizeof(int));
				memcpy(result, image, height*width*sizeof(int));

				applyFilter(filterTag, image, height+2, width+2, result);


				processedLines += height;
					
				//afisare in fisier
				FILE* res_img = fopen (resImg, "w");
				fprintf(res_img, "%s", firstLine);
				fprintf(res_img, "%s", secondLine);
				fprintf(res_img, "%d %d\n%d\n", width,height,maxGV);
				for (int j = 1; j<=height; j++){
					for (int k = 1; k<=width; k++){
						fprintf(res_img, "%d\n", result[j*(width+2)+k]);
					}
				}
				fclose(res_img);
			
					
			}
		
		}

		//trimit mesajul cu tag-ul final
		for (int i = 0; i<nrCopii; i++){
			MPI_Send(hw,2,MPI_INT,copii[i],finalTag,MPI_COMM_WORLD);
		}

		//statistica
		int* statistica = malloc (size * 2 * sizeof(int));
				int nrNoduriStatistica = 0;
				if (nrCopii == 0){ //daca radacina e frunza
					nrNoduriStatistica = 1;
					statistica[0] = rank;
					statistica[1] = processedLines;
					MPI_Send(&nrNoduriStatistica, 1, MPI_INT, parent, 1, MPI_COMM_WORLD);
					MPI_Send(statistica, 2, MPI_INT, parent,1,MPI_COMM_WORLD);
				} else { //daca nu e frunza, primeste statistica de la toti copiii si o asambleaza in vectorul statistica
					int nrNoduriCopil = 0;
					for (int k = 0; k<nrCopii; k++){
						MPI_Recv(&nrNoduriCopil, 1, MPI_INT, MPI_ANY_SOURCE,MPI_ANY_TAG,MPI_COMM_WORLD, &status);
						int * statisticaNod = malloc ( (nrNoduriCopil)* 2 * sizeof(int));
						MPI_Recv(statisticaNod, nrNoduriCopil*2, MPI_INT, status.MPI_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

						for (int i = 0, j = nrNoduriStatistica * 2; i<nrNoduriCopil*2; i++, j++){
							statistica[j] = statisticaNod[i];
						}
						nrNoduriStatistica += nrNoduriCopil;
					}
					statistica[nrNoduriStatistica * 2] = rank;
					statistica[nrNoduriStatistica * 2 + 1] = 0;
					nrNoduriStatistica += 1;
				}

		//scrie statistica in fisier
		FILE* stat_file = fopen(argv[3], "w");
		for (int i = 0; i<size; i++){
			for (int j = 0; j<size*2; j+=2){
				if (statistica[j] == i){
					fprintf(stat_file, "%d: %d\n", i, statistica[j+1]);
				}
			}
		}
		fclose(stat_file);
		
	}



	if (rank != 0){
		while (1){ //cat timp nu primeste tag-ul de final
			int* hwRec = (int*)calloc (2 ,sizeof(int)); 

			//primeste dimensiunile blocului de prelucrat
			MPI_Recv(hwRec, 2, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
			if (status.MPI_TAG == finalTag){
				//daca am primit tag-ul de final, il distribui si incep sa construiesc statistica
				for (int i = 0; i<nrCopii; i++){
					MPI_Send(hwRec,2,MPI_INT,copii[i],finalTag,MPI_COMM_WORLD);
				}		

				int* statistica = malloc (size * 2 * sizeof(int));
				int nrNoduriStatistica = 0;
				if (nrCopii == 0){ //daca nodul e frunza, statistica reprezinta doar liniile procesate de el
					nrNoduriStatistica = 1;
					statistica[0] = rank;
					statistica[1] = processedLines;
					MPI_Send(&nrNoduriStatistica, 1, MPI_INT, parent, 1, MPI_COMM_WORLD);
					MPI_Send(statistica, 2, MPI_INT, parent,1,MPI_COMM_WORLD);
				} else { //daca nodul nu e frunza, asamblez statisticile fiilor si adaug statistica lui, dupa care le trimit catre parinte
					int nrNoduriCopil = 0;
					for (int k = 0; k<nrCopii; k++){
						MPI_Recv(&nrNoduriCopil, 1, MPI_INT, MPI_ANY_SOURCE,MPI_ANY_TAG,MPI_COMM_WORLD, &status);
						int * statisticaNod = malloc ( (nrNoduriCopil)* 2 * sizeof(int));
						MPI_Recv(statisticaNod, nrNoduriCopil*2, MPI_INT, status.MPI_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

					
						for (int i = 0, j = nrNoduriStatistica*2; i<nrNoduriCopil*2; i++, j++){
							statistica[j] = statisticaNod[i];
						}
						nrNoduriStatistica += nrNoduriCopil;
					}
					statistica[nrNoduriStatistica * 2] = rank;
					statistica[nrNoduriStatistica * 2+ 1] = 0;
					nrNoduriStatistica++;
					MPI_Send(&nrNoduriStatistica, 1, MPI_INT, parent, 1, MPI_COMM_WORLD);
					MPI_Send(statistica, nrNoduriStatistica * 2, MPI_INT, parent,1,MPI_COMM_WORLD);

				}

				break;
			}
			height = hwRec[0];
			width = hwRec[1];
			if (height > 0) { //daca se primesc linii de procesat


				int *recv = (int*)calloc(height * width,sizeof(int));

				MPI_Recv(recv, height * width, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD,&status);
				filterTag = status.MPI_TAG;
				if (nrCopii > 0){ //daca nodul e intermediar, il impart copiilor
					int liniiCopil = (height - 2)/nrCopii;
					int liniiSend;
					int* firstLineOfChild = (int*)calloc(size,sizeof(int));
					int rest = (height - 2) % nrCopii;
					int crtLine = 1;
					int nrCopiiActivi = nrCopii;
					for (int j = 0; j<nrCopii; j++){
						if ((rest > 0)&&(j==nrCopii - 1)){
							liniiSend = liniiCopil + rest;
						} else {
							liniiSend = liniiCopil;
						}

						int* hw = (int*)calloc (2,sizeof(int));
								
						if (liniiSend > 0) {

							hw[0] = liniiSend + 2;
							hw[1] = width;
						
							
							int *toSend = (int*)calloc((liniiSend+2)* (width),sizeof(int));
							int l = 0;
							firstLineOfChild[copii[j]] = crtLine -1;
							for (int k = (crtLine - 1)*(width); k<(crtLine+liniiSend+1)*width; k++){
								toSend[l] = recv[k];
								l++;
							}

				
							MPI_Send(hw, 2,MPI_INT, copii[j],1,MPI_COMM_WORLD);
							MPI_Send(toSend,(liniiSend+2)*(width),MPI_INT,copii[j],filterTag,MPI_COMM_WORLD);
							crtLine += liniiSend;

						}  else {
							hw[0] = -1;
							MPI_Send(hw,2,MPI_INT,copii[j],1,MPI_COMM_WORLD);
							nrCopiiActivi--;
						}


						

					}


					//primesc si asamblez rezultatele
					int *result = (int*) calloc (height*width,sizeof(int));
					for (int j = 0; j<nrCopiiActivi; j++){
						MPI_Recv(hwRec,2,MPI_INT,MPI_ANY_SOURCE,MPI_ANY_TAG,MPI_COMM_WORLD, &status);
						recv = (int*)calloc (hwRec[0]*hwRec[1],sizeof(int));
						MPI_Recv(recv, hwRec[0]*hwRec[1], MPI_INT, status.MPI_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
						
						crtLine = firstLineOfChild[status.MPI_SOURCE] + 1;
						int l = width;
						
						for (int k = crtLine*width; k<(crtLine+hwRec[0] - 2)*hwRec[1]; k++){
							result[k] = recv[l];
							l++;	
						
						}
							

					}

					//trimit in sus
					hwRec[0] = height;
					hwRec[1] = width;
					MPI_Send(hwRec,2,MPI_INT, parent, 1, MPI_COMM_WORLD);
					MPI_Send(result, height*width,MPI_INT,parent,1,MPI_COMM_WORLD);

				} else { //frunza
				

					//prelucrare cu filtre
					int* result = (int*)calloc(height*width,sizeof(int));
					memcpy(result, recv, height*width*sizeof(int));

					applyFilter(filterTag, recv, height, width, result);


					processedLines += hwRec[0] - 2;
					

					//trimitere rezultate
					MPI_Send(hwRec,2,MPI_INT,parent, 1, MPI_COMM_WORLD);
					MPI_Send(result, height*width, MPI_INT, parent, 1, MPI_COMM_WORLD);
					
				}

			} else { //daca nodul nu are linii de prelucrat, isi anunta toti fiii
				hwRec[0] = -1;
				for (int i = 0; i<nrCopii; i++){
					MPI_Send(hwRec,2,MPI_INT,copii[i],1,MPI_COMM_WORLD);
				}
			}
		}
		
	}

	//printf ("%d:Am terminat\n", rank);
	fflush(stdout);
	MPI_Finalize();
	return 0;
}

