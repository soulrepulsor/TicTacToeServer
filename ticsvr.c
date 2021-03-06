#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int port = 3000;
static char board[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

struct item
{
    int fd;
    struct in_addr adr;
    struct item *next;
};

struct item *head = NULL;

int main(int argc, char **argv)
{
    extern void insert(int fd, struct in_addr adr);
    extern void showboard(int fd);
    extern char *extractline(char *p, int size);
    extern void delete(int fd);
    extern void assign(int fd, int player_type, int *replacement, int *turn);
    extern void updateboard();
    extern void validmove(int index, int *turn, int p1, int p2, char *adr);
    extern void validgame(int *turn, int *p1, int *p2);
    extern void announcement(char *message);
    extern void singlemessage(int fd, char *message);
    extern void chatmessage(int sender, char *msg);

    int c;
    int status = 0;

    while ((c = getopt(argc, argv, "p:")) != EOF)
    {
        switch (c)
        {
        case 'p':
            port = atoi(optarg);
            break;

        default:
            status = 1;
            break;
        }
    }

    if (status || optind < argc)
    {
        fprintf(stderr, "usage: %s [-p port]\n", argv[0]);
        return 1;
    }

    int turn = -1;
    int fd, clientfd;
    int p1 = -1;
    int p2 = -1;
    socklen_t size;
    struct sockaddr_in r, q;
    int bytes_in_buf = 0;

    if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket");
        return 1;
    }

    memset(&r, '\0', sizeof r);
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&r, sizeof r) < 0)
    {
        perror("bind");
        return 1;
    }

    size = sizeof q;

    int maxfd;
    int s;
    char buf[1000];
    char *nextpos;

    fd_set fds;

    maxfd = 0;
    char *msg;

    while (!listen(fd, 5))
    {
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        maxfd = fd;
        for (struct item *p = head; p; p = p->next)
        {
            FD_SET(p->fd, &fds);
            maxfd = maxfd > p->fd ? maxfd : p->fd;
        }
        if (select(maxfd + 1, &fds, NULL, NULL, NULL) < 0)
        {
            perror("select");
            return 1;
        }

        if (FD_ISSET(fd, &fds))
        {
            if ((clientfd = accept(fd, (struct sockaddr *)&q, &size)) < 0)
            {
                perror("accept");
                return 1;
            }
            struct in_addr adr = ((struct sockaddr_in *)&q)->sin_addr;

            printf("new connection from %s\n", inet_ntoa(adr));
            
            showboard(clientfd);

            if (p1 < 0)
            {
                msg = "You now get to play! You are now x.\r\n";
                p1 = clientfd;
                turn = turn < 0 ? p1 : turn;
                printf("client from %s is now x\n", inet_ntoa(adr));
            }
            else if (p2 < 0)
            {
                msg = "You now get to play! You are now o.\r\n";
                p2 = clientfd;
                printf("client from %s is now o\n", inet_ntoa(adr));
            }

            singlemessage(clientfd, msg);

            if (turn == p1)
                msg = "It's x's turn!\r\n";
            else
                msg = "It's o's turn!\r\n";
            
            singlemessage(clientfd, msg);
            msg = "";
            

            insert(clientfd, adr);
        }

        for (struct item *p = head; p; p = p->next)
        {
            if (FD_ISSET(p->fd, &fds))
            {   
                int complete_string = 1;

                while (complete_string)
                {
                    if ((s = read(p->fd, buf + bytes_in_buf, sizeof buf - bytes_in_buf - 1)) < 0)
                    {
                        perror("read");
                        return 1;
                    }
                    else if (s == 0)
                    {
                        printf("disconnecting client from %s\n", inet_ntoa(p->adr));
                        int temp = p->fd;
                        close(p->fd);
                        delete (p->fd);
                        if (p1 == temp)
                            assign(p2, 0, &p1, &turn);
                        else if (p2 == temp)
                            assign(p1, 1, &p2, &turn);

                        break;
                    }
                    
                    bytes_in_buf += s;
                    while ((nextpos = extractline(buf, bytes_in_buf)))
                    {
                        if (strlen(buf) == 1 && 0 <= buf[0] - '0' && 9 >= buf[0] - '0' && buf[0] != '0')
                        {
                            int index = buf[0] - '0' - 1;
                            if (p1 == p->fd && turn == p1)
                                validmove(index, &turn, p1, p2, inet_ntoa(p->adr));
                            else if (p2 == p->fd && turn == p2)
                                validmove(index, &turn, p1, p2, inet_ntoa(p->adr));
                            else
                            {
                                printf("%s tries to make move %d, but it's not their turn\n", inet_ntoa(p->adr), index + 1);
                                singlemessage(p->fd, "It's not your turn yet!\r\n");
                            }
                            validgame(&turn, &p1, &p2);
                        }
                        else
                            chatmessage(p->fd, buf);

                        bytes_in_buf -= nextpos - buf;
                        memmove(buf, nextpos, bytes_in_buf);
                        complete_string = 0;
                    }
                }
            }
        }
    }

    perror("listen");
    status = 1;

    return status;
}

char *extractline(char *p, int size)
/* returns pointer to string after, or NULL if there isn't an entire
	 * line here.  If non-NULL, original p is now a valid string. */
{
    int nl;
    for (nl = 0; nl < size && p[nl] != '\r' && p[nl] != '\n'; nl++)
        ;
    if (nl == size)
        return (NULL);

    /*
     * There are three cases: either this is a lone \r, a lone \n, or a CRLF.
     */
    if (p[nl] == '\r' && nl + 1 < size && p[nl + 1] == '\n')
    {
        /* CRLF */
        p[nl] = '\0';
        return (p + nl + 2);
    }
    else
    {
        /* lone \n or \r */
        p[nl] = '\0';
        return (p + nl + 1);
    }
}

void announcement(char *message)
{
    for (struct item *p = head; p; p = p->next)
    {
        if (write(p->fd, message, strlen(message)) != strlen(message))
        {
            perror("write");
            exit(1);
        }
    }
}

void singlemessage(int fd, char *message)
{
    if (write(fd, message, strlen(message)) != strlen(message))
    {
        perror("write");
        exit(1);
    }
}

void chatmessage(int sender, char *msg)
{
    char format_msg[strlen(msg) + 3];

    strcpy(format_msg, msg);
    strcat(format_msg, "\r\n");

    for (struct item *p = head; p; p = p->next)
        if (p->fd != sender)
            singlemessage(p->fd, format_msg);

    printf("char message: %s\n", msg);
}

void showboard(int fd)
{
    char buf[100], *bufp, *boardp;
    int col, row;
    // struct client *p;

    for (bufp = buf, col = 0, boardp = board; col < 3; col++)
    {
        for (row = 0; row < 3; row++, bufp += 4)
            sprintf(bufp, " %c |", *boardp++);
        bufp -= 2; // kill last " |"
        strcpy(bufp, "\r\n---+---+---\r\n");
        bufp = strchr(bufp, '\0');
    }
    if (write(fd, buf, bufp - buf) != bufp - buf)
        perror("write");
}

void updateboard()
{
    extern void showboard(int fd);

    for(struct item *p = head; p; p=p->next)
        showboard(p->fd);
}

void validmove(int index, int *turn, int p1, int p2, char *adr)
{
    extern void announcement(char *message);
    char *msg1;
    char *temp;

    if ('0' <= board[index] && board[index] <= '9')
    {
        if (*turn == p1)
        {
            board[index] = 'x';
            printf("%s (x) makes move %d\n", adr, index + 1);
            msg1 = "It's o turn!\r\n";
            temp = "x makes move ";
        }
        else if (*turn == p2)
        {
            board[index] = 'o';
            printf("%s (o) makes move %d\n", adr, index + 1);
            msg1 = "It's x turn!\r\n";
            temp = "o makes move ";
        }

        char msg2[strlen(temp) + 4];
        char rest[4] = {'0' + index + 1, '\r', '\n', '\0'};
        strcpy(msg2, temp);
        strcat(msg2, rest);

        announcement(msg2);
        updateboard();
        announcement(msg1);
        *turn = *turn == p1 ? p2 : p1;
    }
    else
        printf("%s tries to make move %d, but that space is taken\n", adr, index + 1);
}

void validgame(int *turn, int *p1, int *p2)
{
    extern int game_is_over();
    extern void resetboard();

    char c;
    if ((c = game_is_over()))
    {
        printf("Game over!\n");

        if (c == 'x' || c == 'o')
        {
            if (c == 'x')
                announcement("x wins.\r\n");
            else
                announcement("o wins.\r\n");
            printf("%c wins.\n", c);
        }
        else 
        {
            announcement("It's a tie\r\n");
            printf("It's a tie\n");
        }

        announcement("Let's play again!\r\n");

        int temp = *p1;
        *p1 = *p2;
        *p2 = temp;
        *turn = *p1;

        singlemessage(*p1, "You are x.\r\n");
        singlemessage(*p2, "You are o.\r\n");

        resetboard();
    }
}

void resetboard()
{
    extern void updatebaord();
    for (int i = 0; i < 9; i++)
        board[i] = '0' + i + 1;
    updateboard();
}

int game_is_over()
{
    int i, c;
    extern int allthree(int start, int offset);
    extern int isfull();

    for (i = 0; i < 3; i++)
        if ((c = allthree(i, 3)) || (c = allthree(i * 3, 1)))
            return c;
    if ((c = allthree(0, 4)) || (c = allthree(2, 2)))
        return c;
    if (isfull())
        return ' ';
    return 0;
    
}

int allthree(int start, int offset)
{
    if (board[start] > '9' && board[start] == board[start + offset]
            && board[start] == board[start + offset * 2])
        return (board[start]);
    return 0;
}

int isfull()
{
    int i;
    for (i = 0; i < 9; i++)
        if (board[i] < 'a')
            return 0;
    return 1;
}

void insert(int fd, struct in_addr adr)
{
    struct item *new;
    struct item *p;

    if ((new = malloc(sizeof(struct item))) == NULL)
    {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }

    new->fd = fd;
    new->adr = adr;
    new->next = NULL;

    if (head == NULL)
        head = new;
    else
    {
        for (p = head; p->next; p = p->next)
            ;
        p->next = new;
    }
}

void delete (int fd)
{
    struct item *ptr;
    struct item *temp;

    if (!head)
        return;
    else if (head->fd == fd)
    {
        temp = head;
        head = head->next;
        free(temp);
        return;
    }

    for (ptr = head; ptr->next && ptr->next->fd != fd; ptr = ptr->next)
        ;

    if (ptr->next && ptr->next->fd == fd)
    {
        temp = ptr->next;
        ptr->next = ptr->next->next;
        free(temp);
    }
}

void assign(int fd, int player_type, int *replacement, int *turn)
{
    char *msg;
    for (struct item *p = head; p; p = p->next)
    {
        if (p->fd != fd)
        {
            if (player_type)
            {
                printf("client from %s is now o\n", inet_ntoa(p->adr));
                msg = "You now get to play! You are now o.\r\n";
            }
            else
            {
                printf("client from %s is now x\n", inet_ntoa(p->adr));
                msg = "You now get to play! You are now x.\r\n";
            }

            singlemessage(p->fd, msg);

            *turn = *turn == *replacement ? p->fd : *turn;
            *replacement = p->fd;
            return;
        }
    }
    *replacement = -1;
}
