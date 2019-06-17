/**
 * @file examples/win32/testmega/main.cpp
 * @brief Example app for Windows
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#include <megaapi.h>
#include <Windows.h>
#include <iostream>

//ENTER YOUR CREDENTIALS HERE
#define MEGA_EMAIL "kkituyi@yahoo.com"
#define MEGA_PASSWORD "Biologist27$"

//Get yours for free at https://mega.co.nz/#sdk
#define APP_KEY "RokyySSL"
#define USER_AGENT "Example Win32 App"

using namespace mega;
using namespace std;

class MyListener: public MegaListener,public MegaRequestListener
{
public:
	bool finished;
    std::string file_name,save_loc;
    int request_type;
    MyListener() { finished = false; }

    virtual void onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e) {
      if (e->getErrorCode() != MegaError::API_OK) {
        finished = true;
        return;
      }

      switch (request->getType()) {
        case MegaRequest::TYPE_LOGIN: {
          // api->fetchNodes();

          break;
        }
        case MegaRequest::TYPE_GET_PUBLIC_NODE: {
          if (e->getErrorCode() == MegaError::API_OK) {
            bool flag = request->getFlag();
			if(request_type==1){
            MegaNode *file = request->getPublicMegaNode();
            if (file->isFile()) {
              if (file_name.length() <= 0)
                api->startDownload(file, std::string(save_loc + std::string(file->getName())).c_str());
              else {
                api->startDownload(file, std::string(save_loc + file_name).c_str());
              }
            } else {
              std::cout << "Only file downloads supported!" << std::endl;
            }
			}
          }
        }
        case MegaRequest::TYPE_FETCH_NODES: {
          cout << "***** Showing files/folders in the root folder:" << endl;
          MegaNode *root = api->getRootNode();
          MegaNodeList *list = api->getChildren(root);

          for (int i = 0; i < list->size(); i++) {
            MegaNode *node = list->get(i);
            if (node->isFile())
              cout << "*****   File:   ";
            else
              cout << "*****   Folder: ";

            cout << node->getName() << endl;
          }
          cout << "***** Done" << endl;

          delete list;

        if(request_type==2){
          		api->startUpload(file_name.c_str(), root);
		  }
          delete root;

          break;
        }
        default:
          break;
      }
	}

	//Currently, this callback is only valid for the request fetchNodes()
	virtual void onRequestUpdate(MegaApi*api, MegaRequest *request)
	{
		cout << "***** Loading filesystem " <<  request->getTransferredBytes() << " / " << request->getTotalBytes() << endl;
	}

	virtual void onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError* error)
	{
		cout << "***** Temporary error in request: " << error->getErrorString() << endl;
	}

	virtual void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* error)
	{
		if (error->getErrorCode())
		{
			cout << "***** Transfer finished with error: " << error->getErrorString() << endl;
		}
		else
		{
			cout << "***** Transfer finished OK" << endl;
		}

		finished = true;
	}
	
	virtual void onTransferUpdate(MegaApi *api, MegaTransfer *transfer)
	{
		cout << "***** Transfer progress: "<<transfer->getTransferString ()<<" :"  << transfer->getTransferredBytes() << "/" << transfer->getTotalBytes() << endl; 
	}

	virtual void onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError* error)
	{
		cout << "***** Temporary error in transfer: " << error->getErrorString() << endl;
	}

	virtual void onUsersUpdate(MegaApi* api, MegaUserList *users)
	{
		if (users == NULL)
		{
			//Full account reload
			return;
		}
		cout << "***** There are " << users->size() << " new or updated users in your account" << endl;
	}

	virtual void onNodesUpdate(MegaApi* api, MegaNodeList *nodes)
	{
		if(nodes == NULL)
		{
			//Full account reload
			return;
		}

		cout << "***** There are " << nodes->size() << " new or updated node/s in your account" << endl;
	}
};

void upload_file(/* struct shared_variables *global_variables,*/std::string parent, std::string current_file){
	//Check the documentation of MegaApi to know how to enable local caching
	MegaApi *megaApi = new MegaApi(APP_KEY, (const char *)NULL, USER_AGENT);

	//By default, logs are sent to stdout
	//You can use MegaApi::setLoggerObject to receive SDK logs in your app
	megaApi->setLogLevel(MegaApi::LOG_LEVEL_INFO);
        megaApi->setMaxConnections(4);
        MyListener listener;
        listener.file_name =current_file;
		listener.request_type = 2;
		megaApi->addListener(&listener);
    	megaApi->login(MEGA_EMAIL, MEGA_PASSWORD);
                // You can use the main thread to show a GUI or anything else. MegaApi runs in a background thread.
        while (!listener.finished) {
                  Sleep(1000);
	}
	

}
void download_multi(/* /struct shared_variables* global_variables,*/
    std::string url, std::string user_agent,
    std::string cookies, std::string save_loc,
    std::string file_name, int connections)
{
	//Check the documentation of MegaApi to know how to enable local caching
	MegaApi *megaApi = new MegaApi(APP_KEY, (const char *)NULL, USER_AGENT);

	//By default, logs are sent to stdout
	//You can use MegaApi::setLoggerObject to receive SDK logs in your app
	megaApi->setLogLevel(MegaApi::LOG_LEVEL_INFO);
        megaApi->setMaxConnections(connections);
        MyListener listener;
        listener.file_name =file_name;
        listener.save_loc = save_loc;
        listener.request_type = 1;
        //Listener to receive information about all request and transfers
	//It is also possible to register a different listener per request/transfer
	megaApi->addListener(&listener);

	 /* if(!strcmp(MEGA_EMAIL, "EMAIL"))
	{
		cout << "Please enter your email/password at the top of main.cpp" << endl;
		cout << "Press any key to exit the app..." << endl;
		getchar();
		exit(0);
	}*/

	//Login. You can get the result in the onRequestFinish callback of your listener
	//megaApi->login(MEGA_EMAIL, MEGA_PASSWORD);
	megaApi->getPublicNode(url.c_str());	
	//should save the node somewhere in a class so we can download later.
	
	//You can use the main thread to show a GUI or anything else. MegaApi runs in a background thread.
	while(!listener.finished)
	{
		Sleep(1000);
	}


	
}
int main()
{
  download_multi("https://mega.nz/#!p4kBXICT!XBPHpPxYRB0-P_w4NL9ITcKA0lIXxa9PXWyqDFh_WgU", "", "", ".", "", 1);

  return 0;
}