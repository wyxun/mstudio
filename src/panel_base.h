#ifndef PANEL_BASE_H
#define PANEL_BASE_H

struct SharedState;

struct Panel {
    explicit Panel(SharedState& s) : state_(s) {}
    virtual ~Panel() = default;
    virtual const char* Name() const = 0;
    virtual void Render() = 0;

protected:
    SharedState& state_;
};

#endif // PANEL_BASE_H
