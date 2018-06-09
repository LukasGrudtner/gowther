#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <string>
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sstream>
#include <vector>
#include "keyManager.h"
#include "settings.h"
#include "stringHandler.h"
#include "iotAuth.h"
#include "RSAKeyExchange.h"
#include "DiffieHellmanPackage.h"
#include "DHKeyExchange.h"

using namespace std;

/* Definição de todos os possíveis estados da FSM:
    HELLO   :   Aguardando pedido de início de conexão.
    DONE    :   Envia pedido de término de conexão.
    RFT     :   Envia confirmação de término de conexão.        :   Request for Termination
    WDC     :   Aguardando confirmação para término de conexão. :   Waiting Done Confirmation
    RRSA    :   Estado de recepção de chaves RSA;
    SRSA    :   Estado de envio de chaves RSA.
    RDH     :   Estado de recepção de chaves Diffie-Hellman.
    SDH     :   Estado de envio de chaves Diffie-Hellman.
    DT      :   Estado de transferência de dados cifrados.
*/
typedef enum {
    HELLO, DONE, RFT, WDC, RRSA, SRSA, RDH, SDH, DT
} States;

int EXPONENT            = 3;

KeyManager* keyManager;
StringHandler StringHandler;
FDR partnerFDR;
IotAuth iotAuth;
Utils utils;
int partnerIV = 0;

/* Calcula a resposta do FDR recebido por parâmetro. */
int calculateFDRValue(int _iv, FDR* _fdr)
{
    int result = 0;
    if (_fdr->getOperator() == '+') {
        result = _iv+_fdr->getOperand();
    }
    return result;
}

/* Verifica se a resposta do FDR é válida. */
bool checkAnsweredFDR(int answeredFdr)
{
    int answer = calculateFDRValue(keyManager->getMyIV(), keyManager->getMyFDR());
    return answer == answeredFdr;
}

bool checkRequestForTermination(char message[])
{
    char aux[strlen(DONE_MESSAGE)+1];
    aux[strlen(DONE_MESSAGE)] = '\0';
    for (int i = 0; i < strlen(DONE_MESSAGE); i++) {
        aux[i] = message[i];
    }

    /* Verifica se a mensagem recebida é um DONE. */
    if (strcmp(aux, DONE_MESSAGE) == 0) {
        return true;
    } else {
        return false;
    }
}

void wdc(States *state, int socket, struct sockaddr *client, socklen_t size)
{
    char message[512];
    recvfrom(socket, message, sizeof(message), 0, client, &size);

    if (message[0] == DONE_ACK_CHAR) {
        *state = HELLO;
    } else {
        *state = WDC;
    }
}

void rft(States *state, int socket, struct sockaddr *client, socklen_t size)
{
    sendto(socket, DONE_ACK, strlen(DONE_ACK), 0, client, size);
    *state = HELLO;

    if (VERBOSE) {
        printf("\n*******DONE CLIENT AND SERVER******\n");
        printf("Done Client and Server Successful!\n");
        printf("***********************************\n\n");
    }
}

void hello(States *state, int socket, struct sockaddr *client, socklen_t size)
{
    char message[512];
    recvfrom(socket, message, sizeof(message), 0, client, &size);

    if (checkRequestForTermination(message)) {
        *state = RFT;
    } else {
        /* Verifica se a mensagem recebida é um HELLO. */
        if (strcmp(message, HELLO_MESSAGE) == 0) {

            /* Se for, envia um HELLO ACK ao Cliente. */
            int sended = sendto(socket, HELLO_ACK, strlen(HELLO_ACK), 0, client, size);

            /* Se a mensagem foi enviada corretamente, troca o estado para RSAX. */
            if (sended >= 0) {
                *state = RRSA;

                if (VERBOSE) {
                    printf("\n******HELLO CLIENT AND SERVER******\n");
                    printf("Hello Client and Server Successful!\n");
                    printf("***********************************\n\n");
                }

            /* Senão, continua no estado HELLO. */
            } else {
                *state = HELLO;

                if (VERBOSE) {
                    printf("\n******HELLO CLIENT AND SERVER******\n");
                    printf("Hello Client and Server failed!\n");
                    printf("***********************************\n\n");
                }
            }
        }
    }
}

/* Seta as variáveis de controle para o estado de término de conexão. */
void done(States *state, int socket, struct sockaddr *client, socklen_t size)
{
    sendto(socket, DONE_ACK, strlen(DONE_ACK), 0, client, size);
    *state = WDC;
}

void rrsa(States *state, int socket, struct sockaddr *client, socklen_t size)
{
    RSAKeyExchange* rsaReceived = (RSAKeyExchange*)malloc(sizeof(RSAKeyExchange));
    recvfrom(socket, rsaReceived, sizeof(RSAKeyExchange), 0, client, &size);

    /* Realiza a geração das chaves pública e privada (RSA). */
    keyManager->setRSAKeyPair(iotAuth.generateRSAKeyPair());

    /* Gera um IV para o servidor e o armazena no KeyManager. */
    keyManager->setMyIV(iotAuth.generateIV());

    /* Gera uma Função Desafio-Resposta para o servidor e o armazena no KeyManager. */
    keyManager->setMyFDR(iotAuth.generateFDR());

    /* Recebe chave pública do cliente e o IV */
    keyManager->setPartnerPublicKey(rsaReceived->getPublicKey());

    partnerFDR = rsaReceived->getFDR();
    partnerIV = rsaReceived->getIV();

    *state = SRSA;

    if (VERBOSE) {
        printf("******RECEIVED CLIENT RSA KEY******\n");
        cout << "Received: "                << rsaReceived->toString()              << endl;
        cout << "Generated RSA Key: {("     << keyManager->getMyPublicKey().d       << ", "
                                            << keyManager->getMyPublicKey().n       << "), ";
        cout << "("                         << keyManager->getMyPrivateKey().d      << ", "
                                            << keyManager->getMyPrivateKey().n      << ")}" << endl;
        cout << "My IV: "                   << keyManager->getMyIV()                << endl;
        cout << "My FDR: "                  << StringHandler.FdrToString(keyManager->getMyFDR())
                                            << endl                                 << endl;
        cout << "Client RSA Public Key: ("  << keyManager->getPartnerPublicKey().d  << ", "
                                            << keyManager->getPartnerPublicKey().n  << ")" << endl;
        cout << "Client IV: "               << partnerIV                            << endl;
        cout << "Client FDR: "              << StringHandler.FdrToString(&partnerFDR) << endl;
        cout << "Client FDR Answer: "       << calculateFDRValue(partnerIV, &partnerFDR) << endl;
        printf("***********************************\n\n");
    }
}

void srsa(States *state, int socket, struct sockaddr *client, socklen_t size)
{
    /* Envia a chave pública do server e o IV */
    int answerFdr = calculateFDRValue(partnerIV, &partnerFDR);
    RSAKey publicKey = keyManager->getMyPublicKey();
    int iv = keyManager->getMyIV();

    FDR fdr = *keyManager->getMyFDR();

    RSAKeyExchange rsaSent;
    rsaSent.setPublicKey(publicKey);
    rsaSent.setAnswerFDR(answerFdr);
    rsaSent.setIV(iv);
    rsaSent.setFDR(fdr);

    if (VERBOSE) {
        printf("*******SENT SERVER RSA KEY*********\n");
        cout << "Server RSA Public Key: (" << keyManager->getMyPublicKey().d
                  << ", " << keyManager->getMyPublicKey().n << ")" << endl;
        cout << "Answer FDR (Client): " << answerFdr << endl;
        cout << "My IV: " << keyManager->getMyIV() << endl;
        cout << "My FDR: " << StringHandler.FdrToString(keyManager->getMyFDR()) << endl;
        cout << "Sent: " << rsaSent.toString() << endl;
        cout << "***********************************\n" << endl;
    }

    int sended = sendto(socket, (RSAKeyExchange*)&rsaSent, sizeof(rsaSent), 0, client, size);

    *state = RDH;
}

int rdh(States *state, int socket, struct sockaddr *client, socklen_t size)
{
    int *encryptedDHExchange = (int*)malloc(sizeof(DHKeyExchange)*sizeof(int));
    recvfrom(socket, encryptedDHExchange, sizeof(DHKeyExchange)*sizeof(int), 0, client, &size);

    /* Decifra a mensagem com a chave privada do Servidor e a coloca em um array de bytes. */
    byte *decryptedMessage = iotAuth.decryptRSA(encryptedDHExchange, keyManager->getMyPrivateKey(), sizeof(DHKeyExchange));

    /* Converte o array de bytes de volta na struct DHKeyExchange*/
   DHKeyExchange encryptedDHReceived;
   utils.BytesToObject(decryptedMessage, encryptedDHReceived, sizeof(DHKeyExchange));

   /* Extrai o HASH encriptado */
   int *encryptedHash = encryptedDHReceived.getEncryptedHash();

   /* Decifra o HASH com a chave pública do Cliente. */
   byte *decryptedHash = iotAuth.decryptRSA(encryptedHash, keyManager->getPartnerPublicKey(), 128);
   char aux;
   string decryptedHashString = "";
   for (int i = 0; i < 128; i++) {
       aux = decryptedHash[i];
       decryptedHashString += aux;
   }

   cout << "Decrypted Hash: " << decryptedHashString << endl;

    /* Recupera o pacote com os dados Diffie-Hellman do Client. */
    byte* dhPackageBytes = encryptedDHReceived.getDiffieHellmanPackage();
    DiffieHellmanPackage dhPackage;
    utils.BytesToObject(dhPackageBytes, dhPackage, sizeof(DiffieHellmanPackage));

    /* Se o hash for válido, continua com o recebimento. */
    if (iotAuth.isHashValid(dhPackage.toString(), decryptedHashString)) {

        /* Armazena os valores Diffie-Hellman no KeyManager. */
        keyManager->setBase(dhPackage.getBase());
        keyManager->setModulus(dhPackage.getModulus());
        keyManager->setSessionKey(keyManager->getDiffieHellmanKey(dhPackage.getResult()));
        int clientIV = dhPackage.getIV();
        int answeredFdr = dhPackage.getAnswerFDR();

        if (VERBOSE) {
            printf("\n*******CLIENT DH KEY RECEIVED******\n");

            cout << "Hash is valid!" << endl << endl;

            if (VERBOSE_2) {
                cout << "Client Encrypted Data" << endl;
                for (int i = 0; i < sizeof(DHKeyExchange)-1; i++) {
                    cout << encryptedDHExchange[i] << ":";
                }
                cout << encryptedDHExchange[sizeof(DHKeyExchange)-1] << endl << endl;

                cout << "Client Encrypted Hash" << endl;
                for (int i = 0; i < 127; i++) {
                    cout << encryptedHash[i] << ":";
                }
                cout << encryptedHash[127] << endl << endl;
            }

            cout << "Client Decrypted HASH: "   << decryptedHashString          << endl << endl;
            cout << "Diffie-Hellman Key: "      << dhPackage.getResult()        << endl;
            cout << "Base: "                    << dhPackage.getBase()          << endl;
            cout << "Modulus: "                 << dhPackage.getModulus()       << endl;
            cout << "Client IV: "               << clientIV                     << endl;
            cout << "Session Key: "             << keyManager->getSessionKey()  << endl;
            cout << "Answered FDR: "            << answeredFdr                  << endl;
        }

        if (checkAnsweredFDR(answeredFdr)) {
            if (VERBOSE) {
                cout << "Answered FDR ACCEPTED!"                    << endl;
                cout << "**************************************\n"  << endl;
            }
            *state = SDH;
        } else {
            if (VERBOSE) {
                cout << "Answered FDR REJECTED!"                    << endl;
                cout << "ENDING CONECTION..."                       << endl;
                cout << "**************************************\n"  << endl;
            }
            *state = DONE;
        }

    /* Se não, retorna falso e irá ocorrer o término da conexão. */
    } else {
        if (VERBOSE) {
            cout << "Hash is invalid!" << endl << endl;
        }
        *state = DONE;
    }
}

void sdh(States *state, int socket, struct sockaddr *client, socklen_t size)
{
    DiffieHellmanPackage diffieHellmanPackage;
    diffieHellmanPackage.setResult(keyManager->getDiffieHellmanKey());
    diffieHellmanPackage.setBase(keyManager->getBase());
    diffieHellmanPackage.setModulus(keyManager->getModulus());
    diffieHellmanPackage.setIV(keyManager->getMyIV());
    diffieHellmanPackage.setAnswerFDR(calculateFDRValue(keyManager->getMyIV(), keyManager->getMyFDR()));

    /***************************** Geração do HASH *******************************/
    char hashArray[128];
    char messageArray[diffieHellmanPackage.toString().length()];
    memset(hashArray, '\0', sizeof(hashArray));

    /* Converte o pacote (string) para um array de char (messageArray). */
    strncpy(messageArray, diffieHellmanPackage.toString().c_str(), sizeof(messageArray));

    /* Extrai o hash */
    string hash = iotAuth.hash(messageArray);

    /* Encripta o hash utilizando a chave privada do servidor */
    int* encryptedHash = iotAuth.encryptRSA(hash, keyManager->getMyPrivateKey(), hash.length());

    /************************* Preparação do pacote ******************************/

    byte* dhPackageBytes = (byte*)malloc(sizeof(DiffieHellmanPackage));
    utils.ObjectToBytes(diffieHellmanPackage, dhPackageBytes, sizeof(DiffieHellmanPackage));

    DHKeyExchange* dhSent = new DHKeyExchange();
    dhSent->setEncryptedHash(encryptedHash);
    dhSent->setDiffieHellmanPackage(dhPackageBytes);

    /* Converte o objeto dhSent em um array de bytes. */
    byte* dhSentBytes = (byte*)malloc(sizeof(DHKeyExchange));
    utils.ObjectToBytes(*dhSent, dhSentBytes, sizeof(DHKeyExchange));

    /*****************************************************************************/

    /* Cifra a mensagem. */
    int* encryptedMessage = iotAuth.encryptRSA(dhSentBytes, keyManager->getPartnerPublicKey(), sizeof(DHKeyExchange));

    if (VERBOSE) {
        printf("*********SEND SERVER DH KEY********\n\n");

        cout << "Server Hash: "     << hash                                     << endl << endl;
        cout << "Server Package: "  << diffieHellmanPackage.toString()          << endl;

        if (VERBOSE_2) {
            cout << endl    << "Encrypted HASH" << endl;
            for (int i = 0; i < 128; i++) {
                cout << encryptedHash[i] << ":";
            }
            cout << encryptedHash[127]  << endl << endl;

            cout            << "Encrypted Data" << endl;
            for (int i = 0; i < sizeof(DHKeyExchange); i++) {
                cout << encryptedMessage[i] << ":";
            }
            cout << encryptedMessage[127] << endl << endl;
        }
        printf("***********************************\n\n");
    }

    sendto(socket, (int*)encryptedMessage, sizeof(DHKeyExchange)*sizeof(int), 0, client, size);
    *state = DT;
}

void dt(States *state, int socket, struct sockaddr *client, socklen_t size)
{
    char message[512];
    recvfrom(socket, message, sizeof(message), 0, client, &size);

    if (checkRequestForTermination(message)) {
        *state = RFT;
    } else {
        /* Converte o array de chars (buffer) em uma string. */
        string encryptedMessage (message);

        /* Inicialização dos vetores ciphertext. */
        char ciphertextChar[encryptedMessage.length()];
        uint8_t ciphertext[encryptedMessage.length()];
        memset(ciphertext, '\0', encryptedMessage.length());

        /* Converte a mensagem recebida (HEXA) para o array de char ciphertextChar. */
        utils.hexStringToCharArray(encryptedMessage, encryptedMessage.length(), ciphertextChar);

        /* Converte ciphertextChar em um array de uint8_t (ciphertext). */
        utils.charToUint8_t(ciphertextChar, ciphertext, encryptedMessage.length());

        /* Inicialização do vetor plaintext. */
        uint8_t plaintext[encryptedMessage.length()];
        memset(plaintext, '\0', encryptedMessage.length());

        /* Inicialização da chave e iv. */
        uint8_t key[] = { 0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe, 0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81,
                          0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61, 0x08, 0xd7, 0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4 };
        uint8_t iv[]  = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f };

        /* Decifra a mensagem em um vetor de uint8_t. */
        uint8_t *decrypted = iotAuth.decryptAES(ciphertext, key, iv, encryptedMessage.length());
        cout << "Decrypted: " << decrypted << endl;

        *state = DT;
    }
}

void stateMachine(int socket, struct sockaddr *client, socklen_t size)
{
    static States state = HELLO;

    switch (state) {

        case WDC:
            wdc(&state, socket, client, size);
            break;

        case RFT:
            rft(&state, socket, client, size);
            break;

        case DONE:
            done(&state, socket, client, size);
            break;

        case HELLO:
            hello(&state, socket, client, size);
            break;

        case RRSA:
            rrsa(&state, socket, client, size);
            break;

        case SRSA:
            srsa(&state, socket, client, size);
            break;

        case RDH:
            rdh(&state, socket, client, size);
            break;

        case SDH:
            sdh(&state, socket, client, size);
            break;

        case DT:
            dt(&state, socket, client, size);
            break;
    }
}

int main(int argc, char *argv[]){
    keyManager = new KeyManager();
    keyManager->setExponent(EXPONENT);

    struct sockaddr_in cliente, servidor;
    int meuSocket,enviei=0;
    socklen_t tam_cliente;
    // MTU padrão pela IETF
    char buffer[10000];

    meuSocket=socket(PF_INET,SOCK_DGRAM,0);
    servidor.sin_family=AF_INET;
    servidor.sin_port=htons(DEFAULT_PORT);
    servidor.sin_addr.s_addr=INADDR_ANY;

    memset(buffer, 0, sizeof(buffer));

    bind(meuSocket,(struct sockaddr*)&servidor,sizeof(struct sockaddr_in));

    printf("*** Servidor de Mensagens ***\n");
    tam_cliente=sizeof(struct sockaddr_in);

    while(1){
       stateMachine(meuSocket, (struct sockaddr*)&cliente, tam_cliente);
    }

    close(meuSocket);
}
