#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <string>
#include <iostream>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sstream>
#include <vector>
#include "settings.h"
#include "iotAuth.h"

#include "RSA/RSAStorage.h"
#include "RSA/RSAKeyExchange.h"
#include "RSA/RSAPackage.h"

#include "Diffie-Hellman/DiffieHellmanPackage.h"
#include "Diffie-Hellman/DHKeyExchange.h"
#include "Diffie-Hellman/DHStorage.h"
#include "Diffie-Hellman/DHEncPacket.h"

#include "verbose/verbose_server.h"
#include <sys/time.h>

using namespace std;

RSAStorage *rsaStorage;
DHStorage *diffieHellmanStorage;
IotAuth iotAuth;

char *serverIP;
char *clientIP;
int sequence;
char nonceA[129];
char nonceB[129];

double networkTime, processingTime1, processingTime2, tp, auxiliarTime, totalTime;
double t1, t2;
double t_aux1, t_aux2;

/*  Calculate FDR Value
    Calcula a resposta de uma dada FDR. */
int calculateFDRValue(int iv, FDR* fdr)
{
    int result = 0;
    if (fdr->getOperator() == '+') {
        result = iv + fdr->getOperand();
    }
    return result;
}

/*  Check Answered FDR
    Verifica a validade da resposta da FDR gerada pelo Servidor.
*/
bool checkAnsweredFDR(int answeredFdr)
{
    int answer = calculateFDRValue(rsaStorage->getMyPublicKey()->d, rsaStorage->getMyFDR());
    return answer == answeredFdr;
}

/*  Check Request for Termination
    Verifica se a mensagem recebida é um pedido de término de conexão vinda
    do Cliente (DONE).
*/
bool checkRequestForTermination(char* message)
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

/*  Waiting Done Confirmation
    Verifica se a mensagem vinda do Cliente é uma confirmação do pedido de
    fim de conexão enviado pelo Servidor (DONE_ACK).
    Em caso positivo, altera o estado para HELLO, senão, mantém em WDC. 7
*/
void wdc(States *state, int socket, struct sockaddr *client, socklen_t size)
{
    // char message[512];
    // recvfrom(socket, message, sizeof(message), 0, client, &size);

    // if (message[0] == DONE_ACK_CHAR) {
    //     *state = HELLO;
    // } else {
    //     *state = WDC;
    // }
}

/*  Request for Termination
    Envia uma confirmação (DONE_ACK) para o pedido de término de conexão
    vindo do Cliente, e seta o estado para HELLO.
*/
void rft(States *state, int socket, struct sockaddr *client, socklen_t size)
{
    // sendto(socket, DONE_ACK, strlen(DONE_ACK), 0, client, size);
    // *state = HELLO;

    // if (VERBOSE) {rft_verbose();}
}

void generateNonce(char *nonce)
{
    string message = stringTime() + *serverIP + *clientIP + to_string(sequence++);
    string hash = iotAuth.hash(&message);

    memset(nonce, '\0', 129);
    strncpy(nonce, hash.c_str(), 128);
}

void recv_syn(States *state, int socket, struct sockaddr *client, socklen_t size)
{
    structSyn received;
    recvfrom(socket, &received, sizeof(syn), 0, client, &size);

    /* Verifica se a mensagem recebida é um HELLO. */
    if (received.message == SYN) {

        /******************** Store Nonce A ********************/
        strncpy(nonceA, received.nonce, sizeof(nonceA));

        *state = SEND_ACK;
    } else {
        *state = RECV_SYN;
    }

    /******************** Verbose ********************/
    if (VERBOSE) recv_syn_verbose(nonceA);
}

void send_ack(States *state, int socket, struct sockaddr *client, socklen_t size)
{
    /******************** Init Sequence ********************/
    sequence = iotAuth.randomNumber(9999);

    /******************** Generate Nounce B ********************/
    generateNonce(nonceB);

    /******************** Mount Package ********************/
    structAck toSend;
    strncpy(toSend.nonceA, nonceA, sizeof(toSend.nonceA));
    strncpy(toSend.nonceB, nonceB, sizeof(toSend.nonceB));
    
    /******************** Start Network Time ********************/
    struct timeval tv;
    gettimeofday(&tv, NULL);
    t1 = (double)(tv.tv_sec) + (double)(tv.tv_usec)/ 1000000.00;

    /******************** Send Package ********************/
    sendto(socket, &toSend, sizeof(ack), 0, client, size);
    *state = RECV_RSA;

    /******************** Verbose ********************/
    if (VERBOSE) send_ack_verbose(nonceB, sequence, serverIP, clientIP);
}

/*  Done
    Envia um pedido de término de conexão ao Cliente, e seta o estado atual
    para WDC (Waiting Done Confirmation).
*/
void done(States *state, int socket, struct sockaddr *client, socklen_t size)
{
    sendto(socket, DONE_MESSAGE, strlen(DONE_MESSAGE), 0, client, size);
    *state = WDC;
}

string decryptHash(int *encryptedHash)
{
    byte *decryptedHash = iotAuth.decryptRSA(encryptedHash, rsaStorage->getPartnerPublicKey(), 128);

    char aux;
    string decryptedHashString = "";
    for (int i = 0; i < 128; i++) {
        aux = decryptedHash[i];
        decryptedHashString += aux;
    }

    delete[] decryptedHash;

    return decryptedHashString;
}

void recv_rsa(States *state, int socket, struct sockaddr *client, socklen_t size)
{
    /******************** Receive Exchange ********************/
    RSAKeyExchange *rsaReceived = new RSAKeyExchange();
    recvfrom(socket, rsaReceived, sizeof(RSAKeyExchange), 0, client, &size);

    /******************** Stop Network Time ********************/
    struct timeval tv;
    gettimeofday(&tv, NULL);
    t2 = (double)(tv.tv_sec) + (double)(tv.tv_usec)/ 1000000.00;
    networkTime = (double)(t2-t1)*1000;

    /******************** Start Processing Time ********************/
    gettimeofday(&tv, NULL);
    t1 = (double)(tv.tv_sec) + (double)(tv.tv_usec)/ 1000000.00;

    /******************** Store RSA Data ********************/
    RSAPackage rsaPackage = *rsaReceived->getRSAPackage();
    
    rsaStorage = new RSAStorage();
    rsaStorage->setPartnerPublicKey(rsaPackage.getPublicKey());
    rsaStorage->setPartnerFDR(rsaPackage.getFDR());

    /******************** Decrypt Hash ********************/
    string rsaString = rsaPackage.toString();
    string decryptedHash = decryptHash(rsaReceived->getEncryptedHash());

    /******************** Store Nonce A ********************/
    strncpy(nonceA, rsaPackage.getNonceA().c_str(), sizeof(nonceA));

    /******************** Store TP ********************/
    tp = rsaReceived->getProcessingTime();

    /******************** Validity Hash ********************/
    bool isHashValid = iotAuth.isHashValid(&rsaString, &decryptedHash);
    bool isNonceTrue = (rsaPackage.getNonceB() == nonceB);

    if (isHashValid && isNonceTrue) {
        *state = SEND_RSA;
    } else {
        *state = RECV_SYN;
    }

    // /******************** Verbose ********************/
    if (VERBOSE) {recv_rsa_verbose(rsaStorage, nonceA, isHashValid, isNonceTrue);}
}

/*  Send RSA
    Realiza o envio da chave RSA para o Cliente.
*/
void send_rsa(States *state, int socket, struct sockaddr *client, socklen_t size)
{
    /******************** Start Auxiliar Time ********************/
    struct timeval tv;
    gettimeofday(&tv, NULL);
    t_aux1 = (double)(tv.tv_sec) + (double)(tv.tv_usec)/ 1000000.00;

    /******************** Get Answer FDR ********************/
    int answerFdr = calculateFDRValue(rsaStorage->getPartnerPublicKey()->d, rsaStorage->getPartnerFDR());

    /******************** Generate RSA Keys and FDR ********************/
    rsaStorage->setKeyPair(iotAuth.generateRSAKeyPair());
    rsaStorage->setMyFDR(iotAuth.generateFDR());

    /******************** Generate Nonce ********************/
    generateNonce(nonceB);

    /******************** Mount Package ********************/
    RSAPackage rsaSent;
    rsaSent.setPublicKey(*rsaStorage->getMyPublicKey());
    rsaSent.setAnswerFDR(answerFdr);
    rsaSent.setFDR(*rsaStorage->getMyFDR());
    rsaSent.setNonceA(nonceA);
    rsaSent.setNonceB(nonceB);

    /******************** Get Hash ********************/
    string packageString = rsaSent.toString();
    string hash = iotAuth.hash(&packageString);

    /******************** Encrypt Hash ********************/
    int *encryptedHash = iotAuth.encryptRSA(&hash, rsaStorage->getMyPrivateKey(), 128);

    /******************** Stop Processing Time ********************/
    gettimeofday(&tv, NULL);
    t2 = (double)(tv.tv_sec) + (double)(tv.tv_usec)/ 1000000.00;
    processingTime1 = (double)(t2-t1)*1000;

    /******************** Stop Auxiliar Time ********************/
    gettimeofday(&tv, NULL);
    t_aux2 = (double)(tv.tv_sec) + (double)(tv.tv_usec)/ 1000000.00;
    auxiliarTime = (double)(t_aux2-t_aux1)*1000;

    /******************** Rectify Network Time ********************/
    networkTime = networkTime - auxiliarTime;

    /******************** Mount Exchange ********************/
    RSAKeyExchange rsaExchange;
    rsaExchange.setRSAPackage(&rsaSent);
    rsaExchange.setEncryptedHash(encryptedHash);
    rsaExchange.setProcessingTime(processingTime1);

    /******************** Start Total Time ********************/
    gettimeofday(&tv, NULL);
    t1 = (double)(tv.tv_sec) + (double)(tv.tv_usec)/ 1000000.00;

    /******************** Send Exchange ********************/
    sendto(socket, (RSAKeyExchange*)&rsaExchange, sizeof(RSAKeyExchange), 0, client, size);
    *state = RECV_RSA_ACK;

    /******************** Verbose ********************/
    if (VERBOSE) {send_rsa_verbose(rsaStorage, sequence, nonceB);}
}

void recv_rsa_ack(States *state, int socket, struct sockaddr *client, socklen_t size)
{
    RSAKeyExchange *rsaReceived = new RSAKeyExchange();
    recvfrom(socket, rsaReceived, sizeof(RSAKeyExchange), 0, client, &size);

    /******************** Stop Total Time ********************/
    struct timeval tv;
    gettimeofday(&tv, NULL);
    t2 = (double)(tv.tv_sec) + (double)(tv.tv_usec)/ 1000000.00;
    totalTime = (double)(t2-t1)*1000;

    /******************** Proof of Time ********************/
    double limit = processingTime1 + networkTime + (processingTime1 + networkTime)*0.1;

    if (totalTime <= limit) {
        /******************** Get Package ********************/
        RSAPackage rsaPackage = *rsaReceived->getRSAPackage();

        /******************** Decrypt Hash ********************/
        string rsaString = rsaPackage.toString();
        string decryptedHash = decryptHash(rsaReceived->getEncryptedHash());

        /******************** Store Nonce A ********************/
        strncpy(nonceA, rsaPackage.getNonceA().c_str(), sizeof(nonceA));

        bool isHashValid = iotAuth.isHashValid(&rsaString, &decryptedHash);
        bool isNonceTrue = (rsaPackage.getNonceB() == nonceB);
        bool isAnswerCorrect = checkAnsweredFDR(rsaPackage.getAnswerFDR());

        /******************** Validity ********************/
        if (isHashValid && isNonceTrue && isAnswerCorrect) {
            *state = SEND_DH;
        } else {
            *state = RECV_SYN;
        }

        if (VERBOSE) recv_rsa_ack_verbose(nonceA, isHashValid, isAnswerCorrect, isNonceTrue);

    } else {
        if (VERBOSE) time_limit_burst_verbose();
        *state = SEND_SYN;
    }


}

/*  Decrypt DH Key Exchange
    Decifra o pacote de troca Diffie-Hellman utilizando a chave privada do Servidor.
    Recebe por parâmetro a mensagem cifrada e retorna por parâmetro o pacote decifrado.
*/
void decryptDHKeyExchange(int *encryptedMessage, DHKeyExchange *dhKeyExchange)
{
    byte* decryptedMessage = iotAuth.decryptRSA(encryptedMessage, rsaStorage->getMyPrivateKey(), sizeof(DHKeyExchange));
    BytesToObject(decryptedMessage, *dhKeyExchange, sizeof(DHKeyExchange));

    delete[] decryptedMessage;
}

/*  Decrypt Hash
    Decifra o hash obtido do pacote utilizando a chave pública do Cliente.
    Retorna o hash em uma string.
*/
string decryptHash(DHKeyExchange *dhKeyExchange)
{
    int *encryptedHash = dhKeyExchange->getEncryptedHash();
    byte *decryptedHash = iotAuth.decryptRSA(encryptedHash, rsaStorage->getPartnerPublicKey(), 128);

    char aux;
    string decryptedHashString = "";
    for (int i = 0; i < 128; i++) {
        aux = decryptedHash[i];
        decryptedHashString += aux;
    }

    delete[] decryptedHash;

    return decryptedHashString;
}

/*  Setup Diffie-Hellman
    Inicializa os valores pertinentes a troca de chaves Diffie-Hellman:
    expoente, base, módulo, resultado e a chave de sessão.
*/
void generateDiffieHellman()
{
    diffieHellmanStorage = new DHStorage();

    cout << "Generate Diffie Hellman" << endl;
    diffieHellmanStorage->setBase(iotAuth.randomNumber(100)+2);
    diffieHellmanStorage->setExponent(iotAuth.randomNumber(3)+2);
    diffieHellmanStorage->setModulus(iotAuth.randomNumber(100)+2);
}

/*  Receive Diffie-Hellman
    Realiza o recebimento da chave Diffie-Hellman vinda do Cliente.
*/
int recv_dh(States *state, int socket, struct sockaddr *client, socklen_t size)
{
    /******************** Recv Enc Packet ********************/
    DHEncPacket encPacket;
    recvfrom(socket, &encPacket, sizeof(DHEncPacket), 0, client, &size);

    /******************** Stop Total Time ********************/
    struct timeval tv;
    gettimeofday(&tv, NULL);
    t2 = (double)(tv.tv_sec) + (double)(tv.tv_usec)/ 1000000.00;
    totalTime = (double)(t2-t1)*1000;

    /******************** Time of Proof ********************/
    // double limit = networkTime + processingTime2*2;
    double limit = 4000;

    if (totalTime <= limit) {

        /******************** Decrypt Exchange ********************/
        DHKeyExchange dhKeyExchange;
        int *encryptedExchange = encPacket.getEncryptedExchange();
        byte *dhExchangeBytes = iotAuth.decryptRSA(encryptedExchange, rsaStorage->getMyPrivateKey(), sizeof(DHKeyExchange));
        
        BytesToObject(dhExchangeBytes, dhKeyExchange, sizeof(DHKeyExchange));

        /******************** Get DH Package ********************/
        DiffieHellmanPackage dhPackage = dhKeyExchange.getDiffieHellmanPackage();

        /******************** Decrypt Hash ********************/
        string decryptedHash = decryptHash(dhKeyExchange.getEncryptedHash());

        /******************** Validity ********************/
        string dhString = dhPackage.toString();
        bool isHashValid = iotAuth.isHashValid(&dhString, &decryptedHash);
        bool isNonceTrue = (dhPackage.getNonceB() == nonceB);

        if (isHashValid && isNonceTrue) {
            /******************** Store Nounce A ********************/
            strncpy(nonceA, dhPackage.getNonceA().c_str(), sizeof(nonceA));

            /******************** Calculate Session Key ********************/
            diffieHellmanStorage->setSessionKey(diffieHellmanStorage->calculateSessionKey(dhPackage.getResult()));

            *state = SEND_DH_ACK;
        } else {
            *state = SEND_SYN;
        }

        if (VERBOSE) recv_dh_verbose(&dhPackage, diffieHellmanStorage->getSessionKey(), isHashValid, isNonceTrue);

    } else {
        if (VERBOSE) time_limit_burst_verbose();
        *state = SEND_SYN;
    }
}

/*  Send Diffie-Hellman
    Realiza o envio da chave Diffie-Hellman para o Cliente.
*/
void send_dh(States *state, int socket, struct sockaddr *client, socklen_t size)
{
    /******************** Start Processing Time 2 ********************/
    struct timeval tv;
    gettimeofday(&tv, NULL);
    t_aux1 = (double)(tv.tv_sec) + (double)(tv.tv_usec)/ 1000000.00;

    /******************** Generate Diffie-Hellman ********************/
    generateDiffieHellman();

    /******************** Generate Nonce B ********************/
    generateNonce(nonceB);

    /***************** Mount Package ******************/
    DiffieHellmanPackage dhPackage;
    dhPackage.setResult(diffieHellmanStorage->calculateResult());
    dhPackage.setBase(diffieHellmanStorage->getBase());
    dhPackage.setModulus(diffieHellmanStorage->getModulus());
    dhPackage.setNonceA(nonceA);
    dhPackage.setNonceB(nonceB);

    /******************** Get Hash ********************/
    string packageString = dhPackage.toString();
    string hash = iotAuth.hash(&packageString);

    /******************** Encrypt Hash ********************/
    int *encryptedHash = iotAuth.encryptRSA(&hash, rsaStorage->getMyPrivateKey(), hash.length());

    /******************** Mount Exchange ********************/
    DHKeyExchange dhSent;
    dhSent.setEncryptedHash(encryptedHash);
    dhSent.setDiffieHellmanPackage(dhPackage);

    /********************** Serialization Exchange **********************/
    byte *dhExchangeBytes = new byte[sizeof(DHKeyExchange)];
    ObjectToBytes(dhSent, dhExchangeBytes, sizeof(DHKeyExchange));

    /******************** Encryption Exchange ********************/
    int* encryptedExchange = iotAuth.encryptRSA(dhExchangeBytes, rsaStorage->getPartnerPublicKey(), sizeof(DHKeyExchange));
    
    /******************** Stop Processing Time 2 ********************/
    gettimeofday(&tv, NULL);
    t_aux2 = (double)(tv.tv_sec) + (double)(tv.tv_usec)/ 1000000.00;
    processingTime2 = (double)(t2-t1)*1000;

    /******************** Mount Enc Packet ********************/
    DHEncPacket encPacket;
    encPacket.setEncryptedExchange(encryptedExchange);
    
    encPacket.setTP(processingTime2);

    /******************** Start Total Time ********************/
    gettimeofday(&tv, NULL);
    t1 = (double)(tv.tv_sec) + (double)(tv.tv_usec)/ 1000000.00;

    /******************** Send Exchange ********************/
    sendto(socket, (DHEncPacket*)&encPacket, sizeof(DHEncPacket), 0, client, size);
    *state = RECV_DH;

    /******************** Verbose ********************/
    if (VERBOSE) send_dh_verbose(&dhPackage, sequence, encPacket.getTP());

    /******************** Memory Release ********************/
    delete[] encryptedHash;
    delete[] dhExchangeBytes;
    delete[] encryptedExchange;

}

void send_dh_ack(States *state, int socket, struct sockaddr *client, socklen_t size)
{
    /******************** Mount ACK ********************/
    DH_ACK ack;
    ack.message = ACK;
    strncpy(ack.nonce, nonceA, sizeof(ack.nonce));

    // /******************** Serialize ACK ********************/
    byte *ackBytes = new byte[sizeof(DH_ACK)];
    ObjectToBytes(ack, ackBytes, sizeof(DH_ACK));

    /******************** Encrypt ACK ********************/
    int *encryptedAck = iotAuth.encryptRSA(ackBytes, rsaStorage->getMyPrivateKey(), sizeof(DH_ACK));

    /******************** Send ACK ********************/
    sendto(socket, (int*)encryptedAck, sizeof(DH_ACK)*sizeof(int), 0, client, size);

    *state = RECV_DATA;

    /******************** Verbose ********************/
    if (VERBOSE) send_dh_ack_verbose(&ack);
}


/*  Data Transfer
    Realiza a transferência de dados cifrados para o Cliente.
*/
void data_transfer(States *state, int socket, struct sockaddr *client, socklen_t size)
{
    delete rsaStorage;
    /********************* Recebimento dos Dados Cifrados *********************/
    char message[1333];
    memset(message, '\0', sizeof(message));
    recvfrom(socket, message, sizeof(message)-1, 0, client, &size);

    /******************* Verifica Pedido de Fim de Conexão ********************/

    if (checkRequestForTermination(message)) {
        *state = RFT;
    } else {

        /* Converte o array de chars (buffer) em uma string. */
        string encryptedMessage (message);

        /* Inicialização dos vetores ciphertext. */
        char ciphertextChar[encryptedMessage.length()];
        uint8_t ciphertext[encryptedMessage.length()];
        memset(ciphertext, '\0', encryptedMessage.length());

        /* Inicialização do vetor plaintext. */
        uint8_t plaintext[encryptedMessage.length()];
        memset(plaintext, '\0', encryptedMessage.length());

        /* Inicialização da chave e iv. */
        uint8_t key[32];
        for (int i = 0; i < 32; i++) {
            key[i] = diffieHellmanStorage->getSessionKey();
        }

        uint8_t iv[16];
        for (int i = 0; i < 16; i++) {
            iv[i] = diffieHellmanStorage->getSessionKey();
        }

        /* Converte a mensagem recebida (HEXA) para o array de char ciphertextChar. */
        HexStringToCharArray(&encryptedMessage, encryptedMessage.length(), ciphertextChar);

        /* Converte ciphertextChar em um array de uint8_t (ciphertext). */
        CharToUint8_t(ciphertextChar, ciphertext, encryptedMessage.length());

        /* Decifra a mensagem em um vetor de uint8_t. */
        uint8_t *decrypted = iotAuth.decryptAES(ciphertext, key, iv, encryptedMessage.length());
        cout << "Decrypted: " << decrypted << endl;

        *state = RECV_DATA;
        // delete[] decrypted;
    }
}

/*  State Machine
    Realiza o controle do estado atual da FSM.
*/
void stateMachine(int socket, struct sockaddr *client, socklen_t size)
{
    static States state = RECV_SYN;

    switch (state) {

        /* Waiting Done Confirmation */
        case WDC:
        {
            cout << "WAITING DONE CONFIRMATION" << endl;
            wdc(&state, socket, client, size);
            break;
        }

        /* Request For Termination */
        case RFT:
        {
            cout << "REQUEST FOR TERMINATION RECEIVED" << endl;
            rft(&state, socket, client, size);
            break;
        }

        /* Done */
        case DONE:
        {
            cout << "SEND DONE" << endl;
            done(&state, socket, client, size);
            break;
        }

        /* Hello */
        case RECV_SYN:
        {
            recv_syn(&state, socket, client, size);
            break;
        }

        case SEND_ACK:
        {
            send_ack(&state, socket, client, size);
            break;
        }

        /* Receive RSA */
        case RECV_RSA:
        {
            recv_rsa(&state, socket, client, size);
            break;
        }

        /* Send RSA */
        case SEND_RSA:
        {
            send_rsa(&state, socket, client, size);
            break;
        }

        case RECV_RSA_ACK:
        {
            recv_rsa_ack(&state, socket, client, size);
            break;
        }

        /* Send Diffie-Hellman */
        case SEND_DH:
        {
            send_dh(&state, socket, client, size);
            break;
        }

        /* Receive Diffie-Hellman */
        case RECV_DH:
        {
            recv_dh(&state, socket, client, size);
            break;
        }

        case SEND_DH_ACK:
        {
            send_dh_ack(&state, socket, client, size);
            break;
        }

        /* Data Transfer */
        case RECV_DATA:
        {
            data_transfer(&state, socket, client, size);
            break;
        }
    }
}

int main(int argc, char *argv[]){

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

    tam_cliente=sizeof(struct sockaddr_in);

    struct in_addr ipAddrClient = cliente.sin_addr;
    // struct in_addr ipAddrServer = servidor.sin_addr;

    /* Get IP Address Server */
    struct hostent *server;
    char host_name[256];
    gethostname(host_name, sizeof(host_name));
    server = gethostbyname(host_name);
    // char *serverIP;
    serverIP = inet_ntoa(*(struct in_addr *)*server->h_addr_list);

    /* Get IP Address Client */
    struct hostent *client;
    char client_name[256];
    gethostname(client_name, sizeof(client_name));
    client = gethostbyname(client_name);
    // char *clientIP;
    clientIP = inet_ntoa(*(struct in_addr *)*client->h_addr_list);

    while(1){
       stateMachine(meuSocket, (struct sockaddr*)&cliente, tam_cliente);
    }

    close(meuSocket);
}
