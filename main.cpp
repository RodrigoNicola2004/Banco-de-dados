#include <windows.h>
#include <iostream>
#include <ctime>
#include <string>
#include <cstdlib>
#include <vector>
#include <queue>
#include <algorithm>

using namespace std;

struct Transaction;

struct Resource
{
    string item_id;
    int locked_by_tid;                  //(-1 se livre)
    HANDLE mutex;                       // Cadeado para proteger o acesso ao próprio Resource
    queue<Transaction *> waiting_queue; // Fila de transações esperando por este item

    Resource(string name)
    {
        item_id = name;
        locked_by_tid = -1;
        mutex = CreateMutex(NULL, FALSE, NULL); // libera o cadeado
    }
};

Resource *X = new Resource("X");
Resource *Y = new Resource("Y");

struct Transaction
{
    int id;
    time_t timestamp; // marcaçao tempo p/ Wait-Die
    bool active;      // true -> ativo, false -> desativado (precisa reiniciar)

    Transaction(int _id)
    { // inicializa
        id = _id;
        timestamp = time(NULL) + id;
        active = true;
    }

    void restart()
    { // reinicia --> novo timestamp
        cout << "T" << id << " e terminada e vai reiniciar (Timestamp: " << timestamp << ")" << endl;
        timestamp = time(NULL);
        active = true;
    }
};

vector<Transaction *> all_transactions; // Lista de todas as transações criadas no sistema

// transação 't' tenta bloquear o recurso 'r' -> Wait-Die
bool obter_bloqueio(Transaction *t, Resource *r)
{
    // DWORD wait_result;
    bool acquired = false;

    WaitForSingleObject(r->mutex, INFINITE); // trava o cadeado do recurso para acesso seguro
    // cadeado que impede que múltiplas threads acessem o mesmo recurso ao mesmo tempo.
    if (r->locked_by_tid == -1)
    { // rec livre: t ganha bloqueio
        r->locked_by_tid = t->id;      
        cout << "T" << t->id << " obteve o bloqueio de recurso " << r->item_id << endl;
        acquired = true;
    }
    else
    { // rec ocup: add Wait-Die
        cout << "T" << t->id << " esta esperando pelo recurso " << r->item_id << " (mantido por T" << r->locked_by_tid << ")" << endl;
                                        //recusrso já ocupado pelo tx
        Transaction *holder_t = nullptr;
        time_t holder_timestamp = 0;

        for (Transaction *current_t : all_transactions)
        { // encontra quem tem o bloqueio
            if (current_t->id == r->locked_by_tid)
            {
                holder_t = current_t;
                holder_timestamp = current_t->timestamp;
                break;
            }
        }

        if (holder_t == nullptr)
        {
            cout << "Erro: Transacao T" << r->locked_by_tid << " nao encontrada." << endl;
            ReleaseMutex(r->mutex);
            return false;
        }

        // Wait-Die Protocol
        // Detecção
        if (t->timestamp < holder_timestamp)
        {

            cout << "T" << t->id << " (Timestamp: " << t->timestamp << ") aguarda T" << holder_t->id << " (Timestamp: " << holder_timestamp << ") por " << r->item_id << " (Wait-Die)" << endl;
            r->waiting_queue.push(t);
            ReleaseMutex(r->mutex);
            acquired = false;
        }
        else
        {
            cout << "Resolvendo deadlock com Wait-Die..." << endl;
            cout << "T" << t->id << " (Timestamp: " << t->timestamp << ") e mais jovem que T" << holder_t->id
                 << " (Timestamp: " << holder_timestamp << ") e sera finalizada para evitar deadlock (Wait-Die)" << endl;
            cout << "DEADLOCK DETECTADO: T" << t->id << " foi finalizada em virtude de deadlock detectado." << endl;
            t->active = false;
            acquired = false;
        }
    }
    ReleaseMutex(r->mutex);
    return acquired;
}

void liberar_bloq(Transaction *t, Resource *r)
{
    WaitForSingleObject(r->mutex, INFINITE);
    if (r->locked_by_tid == t->id)
    {       //tx termina de escrever -> libera bloqueio
        r->locked_by_tid = -1;
        cout << "T" << t->id << " liberou " << r->item_id << endl;

        if (!r->waiting_queue.empty())
        {
            Transaction *next_t = r->waiting_queue.front();
            r->waiting_queue.pop();
            cout << "T" << next_t->id << " na fila para " << r->item_id << " sera notificada para tentar adquirir." << endl;
        }
    }
    ReleaseMutex(r->mutex);
}

DWORD WINAPI run_transaction(LPVOID param)
{
    Transaction *t = (Transaction *)param;

    while (t->active)
    {
        cout << "T" << t->id << " entrou em execucao (Timestamp: " << t->timestamp << ")" << endl;

        // 100-50 trabalho
        // 50-20  leitura

        Sleep(rand() % 100 + 50);

        if (!obter_bloqueio(t, X))
        { // Tenta bloquear X
            if (!t->active)
            {
                Sleep(rand() % 200 + 100);
                continue;
            }
            Sleep(rand() % 100 + 50);
            continue;
        }

        cout << "T" << t->id << " leu X" << endl;
        Sleep(rand() % 50 + 20);

        Sleep(rand() % 100 + 50);

        if (!obter_bloqueio(t, Y))
        {                       // - - Y , se der ruim libera x e tenta dnv
            liberar_bloq(t, X); // libera x p/ n travar os outros
            if (!t->active)
            {
                Sleep(rand() % 200 + 100);
                continue;
            }
            Sleep(rand() % 100 + 50);
            continue;
        }

        cout << "T" << t->id << " leu Y" << endl;
        Sleep(rand() % 50 + 20);

        cout << "T" << t->id << " escreveu em X e Y" << endl;
        Sleep(rand() % 100 + 50);

        // libera t os dois:
        liberar_bloq(t, X);
        liberar_bloq(t, Y);

        cout << "T" << t->id << " finalizou sua execucao (commit)" << endl;
        break;
    }
    return 0;
}

int main()
{
    srand((unsigned int)time(NULL)); // joga numero aleatorio
    const int NUM_TRANSACTIONS = 5;
    HANDLE threads[NUM_TRANSACTIONS]; // Vetor para as threads

    for (int i = 0; i < NUM_TRANSACTIONS; i++)
    {
        Transaction *new_t = new Transaction(i);
        all_transactions.push_back(new_t);
        threads[i] = CreateThread(NULL, 0, run_transaction, new_t, 0, NULL);
        Sleep(rand() % 150 + 100); // Pequena pausa para variar os timestamps
    }

    // espera as threads terminarem
    WaitForMultipleObjects(NUM_TRANSACTIONS, threads, TRUE, INFINITE);

    // limpar memoria
    for (int i = 0; i < NUM_TRANSACTIONS; i++)
    {
        CloseHandle(threads[i]);
        delete all_transactions[i];
    }
    delete X;
    delete Y;

    return 0;
}
