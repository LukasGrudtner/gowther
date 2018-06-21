#ifndef DH_KEY_EXCHANGE_H
#define DH_KEY_EXCHANGE_H

#include <stdio.h>
#include <string.h>
#include "DiffieHellmanPackage.h"
#include "../settings.h"

class DHKeyExchange
{
    public:
        DHKeyExchange();

        /* Getters */
        int* getEncryptedHash();
        DiffieHellmanPackage getDiffieHellmanPackage();

        /* Setters */
        void setEncryptedHash(int encHash[]);
        void setDiffieHellmanPackage(DiffieHellmanPackage dhPackage);

    private:
        int encryptedHash[128];
        DiffieHellmanPackage diffieHellmanPackage;

};

#endif
