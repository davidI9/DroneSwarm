#ifndef DRONE_H
#define DRONE_H

class Position {
    private:
        int x;
        int y;
        int z;
    
    public:
        int updatePosition(int x_new, int y_new, int z_new);
}

class Drone {
    private: 
        Position pos;
        int speed;
    
    public:
        
}

#endif