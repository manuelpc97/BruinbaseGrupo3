#include "BTreeNode.h"

#include <math.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iostream>
using namespace std;


#define L_PAIR_SIZE (sizeof(RecordId)+sizeof(int))
#define NL_PAIR_SIZE (sizeof(PageId)+sizeof(int))

//LeafNode empieza

BTLeafNode::BTLeafNode(){
  memset(buffer, '\0', PageFile::PAGE_SIZE);
}
/*
 * Read the content of the node from the page pid in the PageFile pf.
 * @param pid[IN] the PageId to read
 * @param pf[IN] PageFile to read from
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::read(PageId pid, const PageFile& pf)
{ 
	return pf.read(pid, buffer);
}
    
/*
 * Write the content of the node to the page pid in the PageFile pf.
 * @param pid[IN] the PageId to write to
 * @param pf[IN] PageFile to write to
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::write(PageId pid, PageFile& pf)
{ 
	 return pf.write(pid, buffer);
}

/*
 * Return the number of keys stored in the node.
 * @return the number of keys in the node
 */
int BTLeafNode::getKeyCount()
{ 
	int keyCounter = 0;
  char* tmp = buffer;

  int curr;
  int i = 4;//int i=0
  //1008 porque el buffer es 1024, pairsize debe ser 12 asi 1008+12 sigifica
  //1020 + PageId lo que es 4 bytes.
  while(i < 1024){
    memcpy(&curr,tmp,sizeof(int));
    if(curr == 0)
        break;
    //incrementar todo
    keyCounter++;
    tmp += L_PAIR_SIZE;
    i += L_PAIR_SIZE;
  }

  if(keyCounter > 1){
    memcpy(&curr,tmp-sizeof(int),sizeof(int));
    if(curr == 0){
        memcpy(&curr,tmp-2*sizeof(int),sizeof(int));
        if(curr == 0){
            keyCounter--;
        }
    }
  }

  return keyCounter;
}

int BTLeafNode::getMaxKeys()
{
  //maxPairs debe ser 85
  int maxPairs = floor((PageFile::PAGE_SIZE-sizeof(PageId))/(L_PAIR_SIZE));
  return maxPairs;
}

/*
 * Insert a (key, rid) pair to the node.
 * @param key[IN] the key to insert
 * @param rid[IN] the RecordId to insert
 * @return 0 if successful. Return an error code if the node is full.
 */
RC BTLeafNode::insert(int key, const RecordId& rid)
{ 
	//si agregar otro key iría sobre el max key count, se retorna que el nodo está lleno
  if(getKeyCount()+1 > getMaxKeys())
    return RC_NODE_FULL;

  //buscar el index de buffer donde queremos insertar
  int insertIndex;
  locate(key,insertIndex);

  //crear un nuevo buffer donde vamos a copiar todo y zero it out
  char* buffer2 = (char*)malloc(PageFile::PAGE_SIZE);
  memset(buffer2, '\0', PageFile::PAGE_SIZE);

  //copiar el buffer hasta donde queremos insertar
  if(insertIndex >= 0)
      memcpy(buffer2,buffer,insertIndex);

  //insert (key, rid) al buffer
  memcpy(buffer2+insertIndex,&key,sizeof(int));
  memcpy(buffer2+insertIndex+sizeof(int),&rid,sizeof(RecordId));

  //copiar el resto del buffer original hacia el buffer2
  //esto incluye el pid del siguiente nodo
  memcpy(buffer2+insertIndex+sizeof(int)+sizeof(RecordId),buffer+insertIndex,(PageFile::PAGE_SIZE-insertIndex-sizeof(int)-sizeof(RecordId)));

  //reemplazar el buffer viejo con el nuevo(buffer2)
  memcpy(buffer,buffer2,PageFile::PAGE_SIZE);

  //se vacía buffer2
  free(buffer2);
  //si llegamos aqui, somos exitosos
  return 0;
}

/*
 * Insert the (key, rid) pair to the node
 * and split the node half and half with sibling.
 * The first key of the sibling node is returned in siblingKey.
 * @param key[IN] the key to insert.
 * @param rid[IN] the RecordId to insert.
 * @param sibling[IN] the sibling node to split with. This node MUST be EMPTY when this function is called.
 * @param siblingKey[OUT] the first key in the sibling node after split.
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::insertAndSplit(int key, const RecordId& rid, 
                              BTLeafNode& sibling, int& siblingKey)
{ 
	 int keyCount = getKeyCount();

    if(getMaxKeys() > keyCount+1)
      return RC_INVALID_ATTRIBUTE;

    if(sibling.getKeyCount() != 0)
      return RC_INVALID_ATTRIBUTE;

    //encontrar el index de buffer donde queremos insertar
    int insertIndex;
    locate(key,insertIndex);

    //crear un nuevo buffer donde vamos a copiar todo y zero it out
    char* buffer2=(char*)malloc(2*(PageFile::PAGE_SIZE));
    memset(buffer2, '\0', (2*PageFile::PAGE_SIZE));

    //copiar el buffer hasta donde queremos insertar
    memcpy(buffer2,buffer,insertIndex);

    //insert (key, rid) al buffer
    memcpy(buffer2+insertIndex,&key,sizeof(int));
    memcpy(buffer2+insertIndex+sizeof(int),&rid,sizeof(RecordId));
    
    //copiar el resto del buffer original al buffer2
    memcpy(buffer2+insertIndex+sizeof(int)+sizeof(RecordId),buffer+insertIndex,(PageFile::PAGE_SIZE-insertIndex));

    //ceiling para que el primer nodo tenga mas que el segundo
    double dKey = keyCount+1;
    double first = ceil((dKey)/2);

    int splitIndex = first*L_PAIR_SIZE;

    //reemplazar buffer viejo con buffer nuevo
    memcpy(buffer,buffer2,splitIndex);

    //reemplazar sibling buffer con nuevo buffer
    memcpy(sibling.buffer,buffer2+splitIndex,PageFile::PAGE_SIZE+L_PAIR_SIZE-splitIndex);

    //zero out el resto de buffer
    memset(buffer+splitIndex,'\0',PageFile::PAGE_SIZE-splitIndex);

    //zero out el resto de sibling.buffer
    memset(sibling.buffer+(PageFile::PAGE_SIZE+L_PAIR_SIZE-splitIndex),'\0',splitIndex-L_PAIR_SIZE);

    //vaciar el buffer temporal
    free(buffer2);

    //copiar el primer key hijo hacia siblingKey
    memcpy(&siblingKey,sibling.buffer,sizeof(int));
    
    return 0;
}

/**
 * If searchKey exists in the node, set eid to the index entry
 * with searchKey and return 0. If not, set eid to the index entry
 * immediately after the largest index key that is smaller than searchKey,
 * and return the error code RC_NO_SUCH_RECORD.
 * Remember that keys inside a B+tree node are always kept sorted.
 * @param searchKey[IN] the key to search for.
 * @param eid[OUT] the index entry number with searchKey or immediately
                   behind the largest key smaller than searchKey.
 * @return 0 if searchKey is found. Otherwise return an error code.
 */
RC BTLeafNode::locate(int searchKey, int& eid)
{ 
	int i = 0;
  int keyCount = getKeyCount();
  if(keyCount == 0){
    eid = i*L_PAIR_SIZE;
    return RC_NO_SUCH_RECORD;
  }

  for(i=0; i<keyCount; i++){
    int currKey;
    memcpy(&currKey,buffer+(i*L_PAIR_SIZE),sizeof(int));
   
    if(searchKey == currKey){
      eid = i*L_PAIR_SIZE;
      return 0;
    }
    else if(searchKey<currKey){
      eid = i*L_PAIR_SIZE;
      return RC_NO_SUCH_RECORD;
    }
  }

  eid = i*L_PAIR_SIZE;

  //si no se encuentra searchKey y no hay keys mas grandes que el
  return RC_NO_SUCH_RECORD; 
}

/*
 * Read the (key, rid) pair from the eid entry.
 * @param eid[IN] the entry number to read the (key, rid) pair from
 * @param key[OUT] the key from the entry
 * @param rid[OUT] the RecordId from the entry
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::readEntry(int eid, int& key, RecordId& rid)
{ 
	if(eid < 0)
    return RC_NO_SUCH_RECORD;

  if(eid >= (getKeyCount()*L_PAIR_SIZE))
    return RC_NO_SUCH_RECORD;

  int shift = eid;
  memcpy(&key,buffer+shift,sizeof(int));
  memcpy(&rid,buffer+shift+sizeof(int),sizeof(RecordId));

  return 0; 
}

/*
 * Return the pid of the next slibling node.
 * @return the PageId of the next sibling node 
 */
PageId BTLeafNode::getNextNodePtr()
{ 
	PageId pid;
  memcpy(&pid, buffer+(getKeyCount()*L_PAIR_SIZE),sizeof(PageId));
  return pid; 
}

/*
 * Set the pid of the next slibling node.
 * @param pid[IN] the PageId of the next sibling node 
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTLeafNode::setNextNodePtr(PageId pid)
{ 
	if(pid < 0)
    return RC_INVALID_PID;
  memcpy(buffer+(getKeyCount()*L_PAIR_SIZE),&pid,sizeof(PageId));
  return 0; 
}

/*
 * Read the content of the node from the page pid in the PageFile pf.
 * @param pid[IN] the PageId to read
 * @param pf[IN] PageFile to read from
 * @return 0 if successful. Return an error code if there is an error.
 */

void BTLeafNode::print()
{
  char* temp=buffer;
  for(int i=0; i<getKeyCount(); i++){
    int tempInt;
    memcpy(&tempInt,temp,sizeof(int));
    cout<<"key: "<<tempInt<<endl;
    temp += L_PAIR_SIZE;
  }
}
   

//*****************************************************************************
BTNonLeafNode::BTNonLeafNode()
{
  //zero out buffer
  memset(buffer, '\0', PageFile::PAGE_SIZE);
  memset(buffer,0,sizeof(int));
}
/*
 * Write the content of the node to the page pid in the PageFile pf.
 * @param pid[IN] the PageId to write to
 * @param pf[IN] PageFile to write to
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTNonLeafNode::write(PageId pid, PageFile& pf)
{ 
	return pf.write(pid,buffer);
}

RC BTNonLeafNode::read(PageId pid, const PageFile& pf)
{ 
	return pf.read(pid,buffer);
}

/*
 * Return the number of keys stored in the node.
 * @return the number of keys in the node
 */
int BTNonLeafNode::getKeyCount()
{ 
  int numKeys;
  memcpy(&numKeys,buffer,sizeof(int));
  return numKeys;
}

int BTNonLeafNode::getMaxKeys()
{
    //maxPairs debe ser 127
    //restar sizeof numKeys y pageid izquierdo
    int maxPairs = floor((PageFile::PAGE_SIZE-sizeof(int)-sizeof(PageId))/(NL_PAIR_SIZE));
    return maxPairs-1;
}
/*
 * Insert a (key, pid) pair to the node.
 * @param key[IN] the key to insert
 * @param pid[IN] the PageId to insert
 * @return 0 if successful. Return an error code if the node is full.
 */
RC BTNonLeafNode::insert(int key, PageId pid)
{ 
	int numKeys = getKeyCount();
  //si agregar otra key, va sobre el max key count, retornar que el nodo está lleno
  if(numKeys+1 > getMaxKeys())
      return RC_NODE_FULL;

  //buscar el index del buffer donde vamos a insertar
  int insertIndex;
  locate(key,insertIndex);

  //si el key fue encontrando, vamos a obtener el index del key
  //else obtenemos el index del primer key que es mas grande
  //insertamos y movemos todo

  //crear un nuevo buffer donde vamos a copiar todo y zero it out
  char* buffer2 = (char*)malloc(PageFile::PAGE_SIZE);
  memset(buffer2, '\0', PageFile::PAGE_SIZE);

  //copiar buffer hasta donde queremos insertar
  memcpy(buffer2,buffer,insertIndex);

  //insert (key, pid) al buffer
  memcpy(buffer2+insertIndex,&key,sizeof(int));
  memcpy(buffer2+insertIndex+sizeof(int),&pid,sizeof(PageId));

  //copiar el resto del buffer original al buffer2
  memcpy(buffer2+insertIndex+sizeof(int)+sizeof(PageId),buffer+insertIndex,(PageFile::PAGE_SIZE-insertIndex-sizeof(int)-sizeof(PageId)));

  //reemplazar el buffer viejo con el nuevo buffer2
  memcpy(buffer,buffer2,PageFile::PAGE_SIZE);

  //vaciar buffer2
  free(buffer2);

  //set los primeros 4 bytes de buffer a numKeys
  numKeys++;
  memcpy(buffer,&numKeys,sizeof(int));
  
  //si llegamos aqui, tenemos exito
  return 0; 
}

/*
 * Insert the (key, pid) pair to the node
 * and split the node half and half with sibling.
 * The middle key after the split is returned in midKey.
 * @param key[IN] the key to insert
 * @param pid[IN] the PageId to insert
 * @param sibling[IN] the sibling node to split with. This node MUST be empty when this function is called.
 * @param midKey[OUT] the key in the middle after the split. This key should be inserted to the parent node.
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTNonLeafNode::insertAndSplit(int key, PageId pid, BTNonLeafNode& sibling, int& midKey)
{ 
	//asumiendo que se llama solo cuando hay overflow
  //implicando que hay getMaxKeys() +1 llaves
  //si el nodo no esta lleno, retornar error
  int keyCount = getKeyCount();

  if(getMaxKeys() > keyCount+1)
    return RC_INVALID_ATTRIBUTE;

  //hermano debe ser un nodo nuevo y vacío
  if (sibling.getKeyCount() != 0)
    return RC_INVALID_ATTRIBUTE;

  //se busca el index de buffer donde queremos insertar
  int insertIndex;
  locate(key,insertIndex);

  //crear un nuevo buffer donde vamos a copiar todo y zero it out
  char* buffer2 = (char*)malloc(2*(PageFile::PAGE_SIZE));
  memset(buffer2, '\0', (2*PageFile::PAGE_SIZE));

  //copiar buffer hasta donde vamos a insertar
  memcpy(buffer2,buffer,insertIndex);

  //insert (key, pid) al buffer
  memcpy(buffer2+insertIndex,&key,sizeof(int));
  memcpy(buffer2+insertIndex+sizeof(int),&pid,sizeof(PageId));
  
  //copiar el resto del buffer original a buffer2
  memcpy(buffer2+insertIndex+sizeof(int)+sizeof(PageId),buffer+insertIndex,(PageFile::PAGE_SIZE-insertIndex));

  //ceiling para que el primer nodo tenga mas que el segundo
  double dKey = keyCount;

  double first = ceil((dKey+1)/2);

  //shift over due al numKeys siendo puestos al comienzo del buffer
  //buffer+splitIndex va a ser el comienzo a la izquierda pageid del split key
  int splitIndex = (first*NL_PAIR_SIZE)+sizeof(int);

  //reemplazar el buffer viejo con el nuevo
  memcpy(buffer,buffer2,splitIndex);

  //reemplazar buffer del hermano con el nuevo buffer
  memcpy(sibling.buffer+sizeof(int),buffer2+splitIndex,PageFile::PAGE_SIZE+NL_PAIR_SIZE-splitIndex);
  
  //zero out el resto del buffer
  memset(buffer+splitIndex,'\0',PageFile::PAGE_SIZE-splitIndex);

  //zero out el resto del sibling.buffer
  memset(sibling.buffer+(PageFile::PAGE_SIZE+NL_PAIR_SIZE-splitIndex+sizeof(PageId)),'\0',splitIndex-NL_PAIR_SIZE-sizeof(PageId));

  //set el numKey del hermano
  int siblingNumKey = keyCount+1-first;
  memcpy(sibling.buffer,&siblingNumKey,sizeof(int));

  //set el nuevo numKey del nodo
  int newKey = first-1;
  memcpy(buffer,&newKey,sizeof(int));

  //vaciar el buffer temporal
  free(buffer2);

  //Ahora que se ha hecho split e insert, se tiene que set midKey y removerlo del nodo

  //set midKey
  int midKeyPos = sizeof(int) + ((first-1)*NL_PAIR_SIZE)+sizeof(PageId);
  memcpy(&midKey,buffer+midKeyPos,sizeof(int));

  //remover midKey del nodo. No debería tener un pageid al lado 
  //porque se le debe haber dado al hermano
  memset(buffer+midKeyPos,'\0',sizeof(int));

  return 0;
}


RC BTNonLeafNode::locate(int searchKey, int& eid)
{ 
  //esto vaa  darnos el index en buffer de la llave para obtener el pageid que cambiamos
  int i;

  for(i = 0; i < getKeyCount(); i++){
    int currKey;
    //comenzando pos cambia por pageid y los 4 bytes que guardan el numero de llaves
    //y los pares que ya pasamos
    memcpy(&currKey,buffer+(i*NL_PAIR_SIZE)+sizeof(PageId)+sizeof(int),sizeof(int));
    if(searchKey == currKey){
      eid = (i*NL_PAIR_SIZE)+sizeof(PageId)+sizeof(int);
      return 0;
    }else if(searchKey < currKey){
      eid = (i*NL_PAIR_SIZE)+sizeof(PageId)+sizeof(int);
      return RC_NO_SUCH_RECORD;
    }
  }
  //si todavia hay espacio en el nodo, retornar index del espacio vacio
  if(i < getMaxKeys()){
    eid = (i*NL_PAIR_SIZE)+sizeof(PageId)+sizeof(int);
    return RC_NO_SUCH_RECORD;
  }

  //no mas espacio en el nodo
  //no encontramos searchKey y no hay llaves mas grandes 
  //set eid al index para insertar en el index
  eid = ((i)*NL_PAIR_SIZE)+sizeof(PageId)+sizeof(int);

  return RC_NO_SUCH_RECORD; 
}

/*
 * Given the searchKey, find the child-node pointer to follow and
 * output it in pid.
 * @param searchKey[IN] the searchKey that is being looked up.
 * @param pid[OUT] the pointer to the child node to follow.
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTNonLeafNode::locateChildPtr(int searchKey, PageId& pid)
{ 
	//numKeys,pageid,key,pageid,key,...pageid
  int tmpKey;
  PageId tmpPid;
  int i;

  int numKeys = getKeyCount();
  for(i=0; i<numKeys; i++){
    memcpy(&tmpKey,buffer+(i*NL_PAIR_SIZE)+sizeof(PageId)+sizeof(int),sizeof(int));
    //si searchKey < tmpKey se toma el pid a la izquierda de tmpKey
    if(searchKey<tmpKey){
      memcpy(&tmpPid,buffer+(i*NL_PAIR_SIZE)+sizeof(int),sizeof(PageId));
      pid = tmpPid;
      return 0;
    }
    //si estamos en el ultimo key, nos movemos al ultimo pid
    //si estamos en el ultimo key y llegamos alli, searchKey deve ser > todas las keys en el nodo
    if(i == numKeys-1){
      memcpy(&tmpPid,buffer+((i+1)*NL_PAIR_SIZE)+sizeof(int),sizeof(PageId));
      pid = tmpPid;
      return 0;
    }
  }

  return RC_NO_SUCH_RECORD;  
}

/*
 * Initialize the root node with (pid1, key, pid2).
 * @param pid1[IN] the first PageId to insert
 * @param key[IN] the key that should be inserted between the two PageIds
 * @param pid2[IN] the PageId to insert behind the key
 * @return 0 if successful. Return an error code if there is an error.
 */
RC BTNonLeafNode::initializeRoot(PageId pid1, int key, PageId pid2)
{ 
	//zero out todo
  memset(buffer, '\0', PageFile::PAGE_SIZE);

  //las raices nuevas solo tienen 1 key
  int numKeys = 1;
  memcpy(buffer,&numKeys,sizeof(int));

  //insertar los pids y keys de root
  //numKeys,pid,key,pid
  memcpy(buffer+sizeof(int),&pid1,sizeof(PageId));
  memcpy(buffer+sizeof(int)+sizeof(PageId),&key,sizeof(int));
  memcpy(buffer+sizeof(int)+sizeof(PageId)+sizeof(int),&pid2,sizeof(PageId));

  return 0;
}

void BTNonLeafNode::print()
{
  return;
  char* temp = buffer;
  int keyss = getKeyCount();
  for(int i=0; i<1024; i+=sizeof(int)){
    int tempInt;
    memcpy(&tempInt,temp,sizeof(int));
    cout<<tempInt<<" ";
    temp += sizeof(int);
  }
  cout<<endl;
}
