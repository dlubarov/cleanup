#include <stdlib.h>
#include <stdio.h>
#include <ncurses.h>
#include <string.h>
#include <dirent.h>
#include <vector>
#include <algorithm>
#include <sys/stat.h>

#define PAD 1
#define LOADING_MSG "Loading..."

using namespace std;


char *loc;
int player_y = 0, player_x = 0;
int main_win_h, main_win_w;
int largest_ent;
WINDOW *main_win;
bool color;

class Building;
vector<Building*> buildings;


// returns a random signed integer in the range [-rad, rad]
int rand_signed(int rad)
{
    return rand() % (rad * 2 + 1) - rad;
}


// djb2; see http://www.cse.yorku.ca/~oz/hash.html
unsigned int djb2_hash(char *str)
{
    unsigned int hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) ^ c;
    return hash;
}


void mvaddstr_besteffort(int y, int x, char *str)
{
    while (*str != '\0')
    {
        mvaddch(y, x, *str);
        ++x; ++str;
    }
}


struct Building
{
    char *name, *info;
    unsigned char d_type;
    int x1, y1, y2, x2, size;

    Building(unsigned char _d_type, char *_name, int _size)
    {
        size = _size;
        d_type = _d_type;
        name = (char*) malloc(strlen(_name) + 1);
        strcpy(name, _name);

        if (size >= 0)
        {
            info = (char*) malloc(256);
            sprintf(info, "%d bytes", size);
        }
        else
            info = 0;

        int h = info? 4 : 3,
            w = max(strlen(name), info? strlen(info) : 0) + 2;

        int rad = 0;
        srand(djb2_hash(name));
        do {
            y1 = rand_signed(rad);
            x1 = rand_signed(rad * 2);
            y2 = y1 + h - 1;
            x2 = x1 + w - 1;
            ++rad;
        } while (badpos());
    }

    ~Building()
    {
        free(name);
        if (info)
            free(info);
    }

    bool contains(int y, int x)
    {
        return y >= y1 && x >= x1 && y <= y2 && x <= x2;
    }

    bool intersects(Building *b)
    {
        return !(x1 > b->x2 + PAD || y1 > b->y2 + PAD || x2 + PAD < b->x1 || y2 + PAD < b->y1);
    }

    bool badpos()
    {
        if (contains(0, 0))
            return 1;
        for (int i = 0; i < buildings.size(); ++i)
            if (intersects(buildings[i]))
                return 1;
        return 0;
    }

    void render()
    {
        if (color)
        {
            int pair = 0;
            float ratio = size / (float) largest_ent;
            if (ratio > 0.3)
                pair = 1;
            if (ratio > 0.6)
                pair = 2;
            if (strcmp(name, "..") == 0)
                pair = 3;
            attron(COLOR_PAIR(pair));
        }

        // compute screen coordinates
        int y1s = main_win_h/2 + y1 - player_y,
            x1s = main_win_w/2 + x1 - player_x,
            y2s = main_win_h/2 + y2 - player_y,
            x2s = main_win_w/2 + x2 - player_x;

        // render text
        mvaddstr_besteffort(y1s + 1, x1s + 1, name);
        if (info)
            mvaddstr_besteffort(y1s + 2, x1s + 1, info);

        // render border
        if (x1s < -1)
        {
            int fix = -1 - x1s;
            mvhline(y1s, x1s + 1 + fix, 0, x2s - x1s - 1 - fix);
            mvhline(y2s, x1s + 1 + fix, 0, x2s - x1s - 1 - fix);
        }
        else
        {
            mvhline(y1s, x1s + 1, 0, x2s - x1s - 1);
            mvhline(y2s, x1s + 1, 0, x2s - x1s - 1);
        }
        if (y1s < -1)
        {
            int fix = -1 - y1s;
            mvvline(y1s + 1 + fix, x1s, 0, y2s - y1s - 1 - fix);
            mvvline(y1s + 1 + fix, x2s, 0, y2s - y1s - 1 - fix);
        }
        else
        {
            mvvline(y1s + 1, x1s, 0, y2s - y1s - 1);
            mvvline(y1s + 1, x2s, 0, y2s - y1s - 1);
        }

        // render corners
        mvaddch(y1s, x1s, '+');
        mvaddch(y1s, x2s, '+');
        mvaddch(y2s, x1s, '+');
        mvaddch(y2s, x2s, '+');

        if (color)
            attrset(0);
    }
};


Building *find_current_building()
{
    for (int i = 0; i < buildings.size(); ++i)
        if (buildings[i]->contains(player_y, player_x))
            return buildings[i];
    return 0;
}


void display()
{
    getmaxyx(main_win, main_win_h, main_win_w);
    clear();

    for (int i = 0; i < buildings.size(); ++i)
        buildings[i]->render();

    // draw player
    mvaddch(main_win_h/2, main_win_w/2, '@');

    mvaddstr(main_win_h - 1, 0, loc);

    refresh();
}


void show_loading()
{
    clear();
    mvaddstr(main_win_h/2, (main_win_w - strlen(LOADING_MSG))/2, LOADING_MSG);
    refresh();
}


int get_dir_size(char *path)
{
    DIR *dir = opendir(path);
    if (dir == 0)
        return -1;

    int sum = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != 0)
    {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        char *child = (char*) malloc(strlen(path) + strlen(ent->d_name) + 2);
        strcpy(child, path);
        strcat(child, "/");
        strcat(child, ent->d_name);
        
        switch (ent->d_type)
        {
            case DT_REG:
                struct stat st;
                if (stat(child, &st) == 0)
                    sum += st.st_size;
                break;

            case DT_DIR:
                sum += get_dir_size(child);
                break;
        }

        free(child);
    }
    closedir(dir);
    return sum;
}


bool load_dir()
{
    show_loading();
    DIR *dir = opendir(loc);
    if (dir == 0)
        return 0;

    player_x = player_y = 0;
    buildings.clear();
    largest_ent = 1;

    struct dirent *ent;
    while ((ent = readdir(dir)) != 0)
    {
        if (strcmp(ent->d_name, ".") == 0)
            continue;

        char *path = (char*) malloc(strlen(loc) + strlen(ent->d_name) + 2);
        strcpy(path, loc);
        strcat(path, "/");
        strcat(path, ent->d_name);

        int size = -1;
        switch (ent->d_type)
        {
            case DT_REG:
                struct stat st;
                if (stat(path, &st) == 0)
                    size = st.st_size;
                break;

            case DT_DIR:
                if (strcmp(ent->d_name, "..") != 0)
                    size = get_dir_size(path);
                break;
        }

        largest_ent = max(largest_ent, size);
        Building *b = new Building(ent->d_type, ent->d_name, size);
        buildings.push_back(b);
        free(path);
    }

    closedir(dir);
    return 1;
}


void handle(int c)
{
    Building *b;

    switch (c)
    {
        case 'd':
            // TODO: delete
            break;

        case 10:
            b = find_current_building();
            if (b != 0 && b->d_type == DT_DIR)
            {
                char *new_loc = (char*) malloc(strlen(loc) + strlen(b->name) + 2);
                strcpy(new_loc, loc);
                strcat(new_loc, "/");
                strcat(new_loc, b->name);
                char *old_loc = loc;
                loc = new_loc;
                if (load_dir())
                    free(old_loc);
                else
                {
                    loc = old_loc;
                    free(new_loc);
                }
            }
            break;

        case KEY_UP:
        case 'k':
            --player_y;
            break;

        case KEY_LEFT:
        case 'h':
            --player_x;
            break;

        case KEY_DOWN:
        case 'j':
            ++player_y;
            break;

        case KEY_RIGHT:
        case 'l':
            ++player_x;
            break;
    }
}


int main(int argc, char *argv[])
{
    main_win = initscr();
    if ((color = has_colors()))
    {
        start_color();
        init_pair(1, COLOR_YELLOW, COLOR_BLACK);
        init_pair(2, COLOR_RED, COLOR_BLACK);
        init_pair(3, COLOR_GREEN, COLOR_BLACK);
    }
    curs_set(0);
    cbreak();
    noecho();
    keypad(stdscr, 1);

    loc = (char*) malloc(2);
    strcpy(loc, ".");
    getmaxyx(main_win, main_win_h, main_win_w);
    load_dir();
    display();

    int c;
    while ((c = getch()) != 'q') {
        handle(c);
        display();
    }

    endwin();
    return 0;
}

