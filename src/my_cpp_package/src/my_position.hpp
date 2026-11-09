#pragma once
#include <string>

class MyPosition {
public:
    MyPosition(double x, double y, const std::string& tag) 
        : x_(x), y_(y), tag_(tag) {}

    void mover(double dx, double dy) {
        x_ += dx;
        y_ += dy;
    }

    double getX() const { return x_; }
    double getY() const { return y_; }
    std::string getTag() const { return tag_; }

private:
    double x_;
    double y_;
    std::string tag_;
};