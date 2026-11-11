#pragma once
#include <string>

class Drone {
    private:
        double x_;
        double y_;
        double z_;

    public:
        Drone(double x, double y, double z)
            : x_(x), y_(y), z_(z) {}
        Drone()
            : x_(0.0), y_(0.0), z_(0.0) {}
        void mover(double dx, double dy, double dz) {
            x_ += dx;
            y_ += dy;
            z_ += dz;
        }

        double getX() const { return x_; }
        double getY() const { return y_; }
        double getZ() const { return z_; }
    
};