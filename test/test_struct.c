#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct Point {
    int x;
    int y;
};

struct Rect {
    struct Point origin;
    int width;
    int height;
};

int area(struct Rect *r) {
    return r->width * r->height;
}

typedef struct {
    char name[32];
    int age;
} Person;

int main(void) {
    /* Struct basics */
    struct Point p = {10, 20};
    printf("Point: (%d, %d)\n", p.x, p.y);

    /* Nested struct */
    struct Rect r;
    r.origin.x = 0;
    r.origin.y = 0;
    r.width = 100;
    r.height = 50;
    printf("Rect area = %d\n", area(&r));

    /* Struct pointer */
    struct Point *pp = &p;
    pp->x = 30;
    printf("Point via ptr: (%d, %d)\n", pp->x, pp->y);

    /* Dynamic allocation */
    struct Point *points = malloc(3 * sizeof(struct Point));
    for (int i = 0; i < 3; i++) {
        points[i].x = i * 10;
        points[i].y = i * 20;
    }
    for (int i = 0; i < 3; i++) {
        printf("points[%d] = (%d, %d)\n", i, points[i].x, points[i].y);
    }
    free(points);

    /* Typedef struct */
    Person person;
    strcpy(person.name, "Alice");
    person.age = 30;
    printf("Person: %s, age %d\n", person.name, person.age);

    return 0;
}
