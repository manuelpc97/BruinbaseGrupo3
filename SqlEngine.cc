/**
 * Copyright (C) 2008 by The Regents of the University of California
 * Redistribution of this file is permitted under the terms of the GNU
 * Public License (GPL).
 *
 * @author Junghoo "John" Cho <cho AT cs.ucla.edu>
 * @date 3/24/2008
 */

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <climits>
#include "Bruinbase.h"
#include "SqlEngine.h"
#include "BTreeIndex.h"

using namespace std;

// external functions and variables for load file and sql command parsing 
extern FILE* sqlin;
int sqlparse(void);


RC SqlEngine::run(FILE* commandline)
{
  fprintf(stdout, "Bruinbase> ");

  // set the command line input and start parsing user input
  sqlin = commandline;
  sqlparse();  // sqlparse() is defined in SqlParser.tab.c generated from
               // SqlParser.y by bison (bison is GNU equivalent of yacc)

  return 0;
}

RC SqlEngine::select(int attr, const string& table, const vector<SelCond>& cond)
{
  cout << "Entro a select" << endl;
  RecordFile rf;   // RecordFile containing the table
  RecordId   rid;  // record cursor for table scanning
  BTreeIndex btree;
  RC     rc;
  int    key;     
  string value;
  int    count;
  int    diff;
  bool shouldPrint;


  // open the table file
  if ((rc = rf.open(table + ".tbl", 'r')) < 0) {
    fprintf(stderr, "Error: table %s does not exist\n", table.c_str());
    return rc;
  }

  // scan the table file from the beginning
  rid.pid = rid.sid = 0;
  count = 0;
  while (rid < rf.endRid()) {
    // read the tuple
    if ((rc = rf.read(rid, key, value)) < 0) {
      fprintf(stderr, "Error: while reading a tuple from table %s\n", table.c_str());
      goto exit_select;
    }
    shouldPrint = true;
    // check the conditions on the tuple
    for (unsigned i = 0; i < cond.size(); i++) {
      // compute the difference between the tuple value and the condition value
      switch (cond[i].attr) {
        case 1:
	       diff = key - atoi(cond[i].value);
	     break;
        case 2:
	       diff = strcmp(value.c_str(), cond[i].value);
	       break;
      }

      // skip the tuple if any condition is not met
      cout << "Key: " << diff << endl;
      switch (cond[i].comp) {       
        case SelCond::EQ:
          if(diff != 0){
            shouldPrint = false;
          }
        break;
        case SelCond::NE:
        if(diff == 0){
          shouldPrint = false;
        }
        break;
        case SelCond::GT:
        if(diff <= 0){
          shouldPrint = false;
        }
        break;
        case SelCond::LT:
        if(diff >= 0){
          shouldPrint = false;
        }
        break;
        case SelCond::GE:
          if(diff < 0){
            shouldPrint = false;
          }
        break;
        case SelCond::LE:
          if(diff > 0){
              shouldPrint = false;
          }
        break;

      }
    }

    // the condition is met for the tuple. 
    // increase matching tuple counter
    count++;

    // print the tuple 
    if(shouldPrint){
      showOutput(attr,key,value);
    }

    ++rid;
  }

  // print matching tuple count if "select count(*)"
  if (attr == 4) {
    fprintf(stdout, "%d\n", count);
  }
  rc = 0;

  // close the table file and return
  exit_select:
  rf.close();
  return rc;
}



RC SqlEngine::load(const string& table, const string& loadfile, bool index)
{
  string tableName = table + ".tbl";
  string treeName = table + ".idx";
  RC rc;
  RecordFile recordFile; 
  RecordId rID; 

  fstream fs; 
  int key;
  string value;
  string line;
  BTreeIndex btree;

  fs.open(loadfile.c_str(), fstream::in);

  if (!fs.is_open()){
    cout << "No se puedo abrir " << loadfile.c_str() << endl;
  }

  if(recordFile.open(tableName, 'w')){
    return RC_FILE_OPEN_FAILED;
  }

  if (index){ 
    rc = recordFile.append(key, value, rID);
    int iterator=0;
    rc=btree.open(treeName,'w');
    if(!rc){
      int iterator=0;

      while(getline(fs,line)){
        rc=parseLoadLine(line,key,value);
        if(rc)
          break;

        rc=recordFile.append(key,value,rID);
        if(rc)
          break;

        rc=btree.insert(key,rID);
        if(rc)
          break;
      }
      btree.close();
    }
  }else{ 
    while(!fs.eof()){
      getline(fs, line);
      rc = parseLoadLine(line, key, value);
      if (rc){
        break;
      }
      rc = recordFile.append(key, value, rID);
      if (rc){
        break;
      }
    }
  }

  fs.close();

  if (recordFile.close()){
    return RC_FILE_CLOSE_FAILED;
  }

  //retorna 0 si se hace apropiadamente, sino retorna errorcode.
  return rc;

  return 0;
}

RC SqlEngine::parseLoadLine(const string& line, int& key, string& value)
{
    const char *s;
    char        c;
    string::size_type loc;
    
    // ignore beginning white spaces
    c = *(s = line.c_str());
    while (c == ' ' || c == '\t') { c = *++s; }

    // get the integer key value
    key = atoi(s);

    // look for comma
    s = strchr(s, ',');
    if (s == NULL) { return RC_INVALID_FILE_FORMAT; }

    // ignore white spaces
    do { c = *++s; } while (c == ' ' || c == '\t');
    
    // if there is nothing left, set the value to empty string
    if (c == 0) { 
        value.erase();
        return 0;
    }

    // is the value field delimited by ' or "?
    if (c == '\'' || c == '"') {
        s++;
    } else {
        c = '\n';
    }

    // get the value string
    value.assign(s);
    loc = value.find(c, 0);
    if (loc != string::npos) { value.erase(loc); }

    return 0;
}


RC SqlEngine::showOutput(int attr, int key, string value){
  if(attr == 1){
    cout << key << endl;
  }else if(attr == 2){
    cout << value.c_str() << endl;
  }else if(attr == 3){
    cout << key << " , " << value.c_str() << endl;
  }
  return 0;
}

