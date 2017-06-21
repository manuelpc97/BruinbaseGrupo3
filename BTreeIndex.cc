/*
 * Copyright (C) 2008 by The Regents of the University of California
 * Redistribution of this file is permitted under the terms of the GNU
 * Public License (GPL).
 *
 * @author Junghoo "John" Cho <cho AT cs.ucla.edu>
 * @date 3/24/2008
 */
 
#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <cstring> 
#include "BTreeIndex.h"
#include "BTreeNode.h"

using namespace std;

/*
 * BTreeIndex constructor
 */
BTreeIndex::BTreeIndex()
{
    rootPid = 0;

    //al crearse un arbol, no hay root asi que la altura es 0
	treeHeight = 0;
}

/*
 * Open the index file in read or write mode.
 * Under 'w' mode, the index file should be created if it does not exist.
 * @param indexname[IN] the name of the index file
 * @param mode[IN] 'r' for read, 'w' for write
 * @return error code. 0 if no error
 */
RC BTreeIndex::open(const string& indexname, char mode)
{
    //el buffer es para guardar info
		char buffer[PageFile::PAGE_SIZE];
		memset(buffer, '\0', PageFile::PAGE_SIZE);

		//se abre el pagefile y se revisa si hay error
		RC rc = pf.open(indexname, mode);
		if (rc){
			return rc;
		}

		//primero se revisa el último pageid
		if (pf.endPid() <= 0){
			//el arbol es nuevo, se set a 0
			rootPid = 0;
			treeHeight = 0;
		}else{
			//sino, se lee el pagefile
			rc = pf.read(0, buffer);

			//se revisa por error
			if (rc){
				return rc;
			}

			memcpy(&treeHeight, buffer + PageFile::PAGE_SIZE - sizeof(int), sizeof(int));
		}

		//todo salió bien
    return 0;
}

/*
 * Close the index file.
 * @return error code. 0 if no error
 */
RC BTreeIndex::close()
{
    RC rc = pf.close();
    return rc;
}

/*
 * Insert (key, RecordId) pair to the index.
 * @param key[IN] the key for the value inserted into the index
 * @param rid[IN] the RecordId for the record being inserted into the index
 * @return error code. 0 if no error
 */
RC BTreeIndex::insert(int key, const RecordId& rid)
{
    RC rc;

	//si no existe un root, se crea.
	if (treeHeight == 0){
		//al principio, la raíz es un leafnode
		BTLeafNode newTreeRoot;
		//se inserta el key y el recordid
		newTreeRoot.insert(key, rid);
		//se asigna el rootpid
		rootPid = pf.endPid();
		//ahora el arbol tiene una altura de 1 por el root
		treeHeight = 1;
		memcpy(newTreeRoot.getBuffer()+PageFile::PAGE_SIZE-sizeof(int),&treeHeight,sizeof(int));
		//ahora se escribe la data a disco
		newTreeRoot.write(rootPid,pf);

		return 0;
	}else{//else root existe, insertar recursivamente
    //variables para pasar data al overflow de root
    int pKey;
    PageId pPid;

    //intentar de insertar recursivamente
    rc = insertRecursively(key,rid,1,rootPid,pKey,pPid);
    //fallo
    if(rc && rc!=1000)
        return rc;

    //exito
    if(!rc)
        return rc;

    //overflow en el nivel del nodo
    if(rc == 1000){
      if(treeHeight == 1){//root es hoja
        BTLeafNode newLeaf;
        newLeaf.read(rootPid,pf);
        PageId newLeafPid = pf.endPid();
        newLeaf.setNextNodePtr(pPid);
        newLeaf.write(newLeafPid,pf);
        
        BTNonLeafNode rootNode;
        rootNode.initializeRoot(newLeafPid,pKey,pPid);

        //incrementa treeHeight
        treeHeight += 1;
        memcpy(rootNode.getBuffer()+PageFile::PAGE_SIZE-sizeof(int),&treeHeight,sizeof(int));

        //escribir data al disco
        rootNode.write(rootPid,pf);

      }else{//root es nonleaf 
        BTNonLeafNode newNode;
        newNode.read(rootPid,pf);
        PageId newNodePid = pf.endPid();
        newNode.write(newNodePid,pf);
        
        BTNonLeafNode rootNode;
        rootNode.initializeRoot(newNodePid,pKey,pPid);

        //incrementa treeHeight
        treeHeight += 1;
        memcpy(rootNode.getBuffer()+PageFile::PAGE_SIZE-sizeof(int),&treeHeight,sizeof(int));
        //escribir data al disco
        rootNode.write(rootPid,pf);
      }

      //exito
      return 0;
    }

  }

  return -1;
}

/**
 * Run the standard B+Tree key search algorithm and identify the
 * leaf node where searchKey may exist. If an index entry with
 * searchKey exists in the leaf node, set IndexCursor to its location
 * (i.e., IndexCursor.pid = PageId of the leaf node, and
 * IndexCursor.eid = the searchKey index entry number.) and return 0.
 * If not, set IndexCursor.pid = PageId of the leaf node and
 * IndexCursor.eid = the index entry immediately after the largest
 * index key that is smaller than searchKey, and return the error
 * code RC_NO_SUCH_RECORD.
 * Using the returned "IndexCursor", you will have to call readForward()
 * to retrieve the actual (key, rid) pair from the index.
 * @param key[IN] the key to find
 * @param cursor[OUT] the cursor pointing to the index entry with
 *                    searchKey or immediately behind the largest key
 *                    smaller than searchKey.
 * @return 0 if searchKey is found. Othewise an error code
 */
RC BTreeIndex::locate(int searchKey, IndexCursor& cursor)
{
    RC rc;
  PageId pid = rootPid;
  int tempEid =- 1;
  int searchPid;
  BTLeafNode lNode;

  //root es hoja

  if(treeHeight == 1){
    if(cursor.pid == 0){
        return 0;
    }
    rc = lNode.read(pid,pf);
    if(rc)
        return rc;

    rc = lNode.locate(searchKey,tempEid);

    //si es exitoso o falla, localize va a asignar tempEidal valor correcto
    cursor.pid = pid;
    cursor.eid = tempEid/(sizeof(RecordId)+sizeof(int));
    return rc;
  }else{//root no es hoja
    rc = locateRecursively(searchKey,pid,tempEid,1);
    cursor.pid = pid;
    cursor.eid = tempEid/(sizeof(RecordId)+sizeof(int));
    return rc;
  }
  return 0;
}

/*
 * Read the (key, rid) pair at the location specified by the index cursor,
 * and move foward the cursor to the next entry.
 * @param cursor[IN/OUT] the cursor pointing to an leaf-node index entry in the b+tree
 * @param key[OUT] the key stored at the index cursor location.
 * @param rid[OUT] the RecordId stored at the index cursor location.
 * @return error code. 0 if no error
 */
RC BTreeIndex::readForward(IndexCursor& cursor, int& key, RecordId& rid)
{
    RC rc;
  BTLeafNode lNode;
  PageId pid = cursor.pid;
  int eid = cursor.eid;

  //read into node
  rc = lNode.read(pid,pf);

  if(rc)
      return rc;

  //se lee la data del nodo hacia el key y el rid
  int temp_eid = eid*(sizeof(RecordId)+sizeof(int));
  rc = lNode.readEntry(temp_eid,key,rid);

  if(rc)
      return rc;

  if((eid+1) >= (lNode.getKeyCount())){
    //el siguiente index debe ser en el siguiente nodo
    pid = lNode.getNextNodePtr();
    if(pid == 0){
      eid=-1;
    }else
      eid=0;
  }else{
      eid+=1;
  }

  //se asigna la info del cursor
  cursor.pid = pid;
  cursor.eid = eid;

  return 0;
}
//***********************************************************************************
RC BTreeIndex::insertRecursively(int key, const RecordId& rid, int currHeight, PageId pid, int& pKey, PageId& pPid)
{
  RC rc;
  BTLeafNode currLeaf;
  int sibKey;
  PageId sibPid;
  BTNonLeafNode nonLeaf;
  PageId nextPid;
  //el noodo actual es nodo hoja
  if(currHeight == treeHeight){
    BTLeafNode sibNode;
    //obtiene la data del nodo de la hoja actual
    currLeaf.read(pid,pf);

    //intenta insertar
    rc = currLeaf.insert(key,rid);
    if(!rc){
      //exito: escribir y retornar
      if(treeHeight == 1)
        memcpy(currLeaf.getBuffer()+PageFile::PAGE_SIZE-sizeof(int),&treeHeight,sizeof(int));
      currLeaf.write(pid,pf);
      return rc;
    }
    //fallo: overflow
    //insertAndSplit ya que hay overflow
    if(treeHeight == 1){
      int tempnum = 0; 
      memcpy(currLeaf.getBuffer()+PageFile::PAGE_SIZE-sizeof(int),&tempnum,sizeof(int));
    }
    rc = currLeaf.insertAndSplit(key,rid,sibNode,sibKey);

    //retorna si falla
    if(rc)
      return rc;

    //insertAndSplit exitoso
    //inserción al nodo padre será hecho por el caller

    //set nextNodePtrs
    //insertAndSplit automaticamente transfiere el ptr del siguiente nodo al nodo hermano

    sibPid = pf.endPid();

    //set el siguiente pid de hoja al nodo de la hoja actual 
    currLeaf.setNextNodePtr(sibPid);
   
    //actualiza la data de la hoja actual
    currLeaf.write(pid,pf);

    //actualiza la data de la hoja hermana
    sibNode.write(sibPid,pf);

    //se transfiere a las variables
    pKey = sibKey;
    pPid = sibPid;
    
    //exitoso pero se tiene que decir al caller que inserte al padre
    return 1000;

  }else{
		//nodo qque no es hoja, continúa traversalmente

    BTNonLeafNode sibNode;
    //se localiza al hijo para llamarlo recursivamente
    nonLeaf.read(pid,pf);

    rc = nonLeaf.locateChildPtr(key,nextPid);

    //retornar si hay fallo
    if(rc)
        return rc;

    //se inserta recursivamente al hijo
    rc = insertRecursively(key,rid,currHeight+1,nextPid,pKey,pPid);

    //insertion failed
    if(rc && rc != 1000)
        return rc;

    //la inserción es exitosa pero ocurrió split, se arregla el overflow
    if(rc == 1000){
      //se intenta insertar
      rc=nonLeaf.insert(pKey,pPid);
      if(!rc){//todo sucedio bien
        if(pid == 0)
            memcpy(nonLeaf.getBuffer()+PageFile::PAGE_SIZE-sizeof(int),&treeHeight,sizeof(int));
        nonLeaf.write(pid,pf);
        return 0;
      }else{//hay error de overflow
        nonLeaf.insertAndSplit(pKey,pPid,sibNode,sibKey);
        sibPid = pf.endPid();
        //escribir la data
        sibNode.write(sibPid,pf);
        nonLeaf.write(pid,pf);

        //transferir data a las variables pKey y pPid
        pKey = sibKey;
        pPid = sibPid;

        return 1000;
      }
    }
    //todo sucedio bien
    return rc;
  }

}

RC BTreeIndex::locateRecursively(int searchKey, PageId& pid, PageId& eid, int currHeight){
  RC rc;
  BTNonLeafNode nlNode;
  BTLeafNode lNode;
  //busca el nodo
  rc = nlNode.read(pid,pf);
  if(rc)
    return rc;

  //asigna pid al siguiente pid
  rc = nlNode.locateChildPtr(searchKey,pid);

  //if el siguiente es hoja
  if(currHeight + 1 == treeHeight){
    rc = lNode.read(pid,pf);

    if(rc)
      return rc;

    rc = lNode.locate(searchKey,eid);
    return rc;
  }else{
    rc = locateRecursively(searchKey,pid,eid,currHeight+1);

    return rc;
  }
}

void BTreeIndex::print(){
  if(treeHeight == 1){
    BTLeafNode root;
    root.read(rootPid,pf);
    root.print();
  }else if(treeHeight > 1){
    printRecNL(rootPid,1);
  }
}

void BTreeIndex::printRecNL(PageId pid,int heightLevel){
  BTNonLeafNode nonLeaf;
  nonLeaf.read(pid,pf);

  nonLeaf.print();
  PageId first;
  memcpy(&first,nonLeaf.getBuffer()+sizeof(int),sizeof(PageId));

  if(heightLevel + 1 == treeHeight){
    printLeaf(first);
  }else{
    int temporary = nonLeaf.getKeyCount();
    for(int a = 0; a < temporary + 1; a++){
      //llamado recursivo de los hijos
      printRecNL(first, heightLevel+1);

      memcpy(&first,nonLeaf.getBuffer()+sizeof(int)+((a+1)*(sizeof(PageId)+sizeof(int))),sizeof(PageId));
    }
  }

}

void BTreeIndex::printLeaf(PageId pid){
  BTLeafNode firstLeaf;
  firstLeaf.read(pid,pf);
  firstLeaf.print();
  PageId tempPid;
  memcpy(&tempPid,firstLeaf.getBuffer()+(firstLeaf.getKeyCount()*(sizeof(RecordId)+sizeof(int))),sizeof(PageId));

  if(pid != 0 && tempPid != 0 && tempPid < 10000)
    printLeaf(tempPid);
  return;
}
