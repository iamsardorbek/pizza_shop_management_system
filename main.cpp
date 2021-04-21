#include <iostream>
#include <queue>
#include <fstream>
#include <thread>
#include <mutex>
#include <unistd.h>

//Constants for the case when we simulate a small shop (problem_id = 0)
#define SMALL_SHOP_PROBLEM_ID 0
#define SMALL_SHOP_N_OVENS 1
#define SMALL_SHOP_TIMESTAMPS_LARGE_PIZZA 8
#define SMALL_SHOP_TIMESTAMPS_SMALL_PIZZA 5

//Constants for the case when we simulate a large shop (problem_id = 1)
#define LARGE_SHOP_PROBLEM_ID 1
#define LARGE_SHOP_N_OVENS 4
#define LARGE_SHOP_TIMESTAMPS_LARGE_PIZZA 6
#define LARGE_SHOP_TIMESTAMPS_SMALL_PIZZA 4

//Timestamp constants
#define TIMESTAMPS_TO_EAT_LARGE_PIZZA 6
#define TIMESTAMPS_TO_EAT_SMALL_PIZZA 4
#define TIMESTAMP_A 2000 //milliseconds
#define TIMESTAMP_B 200 //milliseconds

//Constant true for both types of shops
#define NUMBER_OF_TABLES 11

int problem_id, n_ovens, timestamps_large_pizza_cooking, timestamps_small_pizza_cooking;
int total_num_of_orders; //this variable takes part in termination condition

using namespace std;

//enums
enum pizza_status_enum {not_ready, ready};
enum pizza_size_enum {SMALL, LARGE};
enum oven_status_enum {COOKING, EMPTY};

//classes
class Order{
public:
    int number_of_customers;
    int index;
    int issue_time; //in timestamps
    int n_large_pizzas_ordered; //number of large pizzas ordered
    int n_small_pizzas_ordered; //number of small pizzas ordered
    //two attributes below will change as the oven cook the pizzas
    int n_large_pizzas_ready; //number of large pizzas ready (cooked)
    int n_small_pizzas_ready; //number of small pizzas ready (cooked)

    //Constructor
    Order(int index_p, int issue_time_p, int number_of_customers_p, int n_large_pizzas_ordered_p, int n_small_pizzas_ordered_p)
    {
        index = index_p;
        issue_time = issue_time_p;
        number_of_customers = number_of_customers_p;
        n_large_pizzas_ordered = n_large_pizzas_ordered_p;
        n_small_pizzas_ordered = n_small_pizzas_ordered_p;
        n_large_pizzas_ready = 0;
        n_small_pizzas_ready = 0;
    }

    //two functions below will be called by manage_oven threads and increment
    void increment_large_pizzas_ready()
    {
      n_large_pizzas_ready++;
    }

    void increment_small_pizzas_ready()
    {
      n_small_pizzas_ready++;
    }

    //it checks whether the order is ready for customers fully, if yes, customers will be seated
    bool is_order_completed()
    {
      if(n_large_pizzas_ordered == n_large_pizzas_ready and n_small_pizzas_ordered == n_small_pizzas_ready)
      {
          return true;
      }
      else
      {
          return false;
      }
    }
};

class Pizza{
public:
    int time_to_eat;
    Order *order; //the order which the pizza is affliated with, we use pointer type object because we work with the order attributes through the pizza object
    pizza_size_enum pizza_size;
    pizza_status_enum status;

    //Constructor
    Pizza(pizza_size_enum pizza_size, Order * order_param) // not sure ab pointers here
    {
        this->pizza_size = pizza_size;
        order = order_param; //not sure about pointers here
        if(pizza_size == pizza_size_enum::LARGE)
        {
            time_to_eat = TIMESTAMPS_TO_EAT_LARGE_PIZZA;
        }
        else if(pizza_size == pizza_size_enum::SMALL)
        {
            time_to_eat = TIMESTAMPS_TO_EAT_SMALL_PIZZA;
        }
        status = pizza_status_enum::not_ready;
    }
};

class Oven{
public:
    queue <Pizza> pizzas; //queue of pizzas to be cooked
    oven_status_enum status;
    int queue_total_time; //total time left for oven to get empty

    //Constructor
    Oven()
    {
        queue_total_time = 0;
        status = oven_status_enum::EMPTY;
    }
};

class Table{
public:
    int capacity_people; //how many people can sit around this table
    int free_spaces; //how many free seats are available now

    //Constructor
    Table(int cap_ppl)
    {
        capacity_people = cap_ppl;
        free_spaces = cap_ppl;
    }
    //Empty constructor, needed when we create an array of Table objects
    Table(){}
};

//global data
queue <Order> orders;
Table tables[NUMBER_OF_TABLES];
vector<Oven> ovens;
vector<thread> threads_to_be_joined;

//Mutexes to synchronize operations on data
mutex mtx_cout, mtx_orders, mtx_tables, mtx_ovens;

//function prototypes
int find_least_busy_oven(); //returns the index of oven which has the shortest queue total time
void group_of_customers_on_a_table_func(Order order, Table & table, int table_index); //thread function that corresponds to a
                                                            //group of customers eating their pizzas on a table
void find_relevant_table(Order & order); //thread function that effectively finds a table for customers in a certain order.
                        //If all tables are busy, the functions wait until a relevant one gets free and makes
                        //customers seated on that table
void manage_oven(int oven_index); //thread function that manages the operation of a separate oven
void assign_order_pizzas_to_less_busier_ovens(Order * order); //effectively assigns each pizza of a certain order to ovens
void launch_orders(); //for every order runs assign_order_pizzas_to_less_busier_ovens when their respective timestamp arrives

int main()
{
    //Each table's parameters where given individually
    //we can also put the sizes of tables in an int array and create objects in a loop
    int table_sizes[NUMBER_OF_TABLES] = {8, 20, 4, 4, 4, 4, 1, 1, 1, 1, 1};
    for(int i = 0; i < NUMBER_OF_TABLES; i++)
    {
        tables[i] = Table(table_sizes[i]);
    }
    //initial code was as below
//    tables[0] = Table(8);
//    tables[1] = Table(20);
//    tables[2] = Table(4);
//    tables[3] = Table(4);
//    tables[4] = Table(4);
//    tables[5] = Table(4);
//    tables[6] = Table(1);
//    tables[7] = Table(1);
//    tables[8] = Table(1);
//    tables[9] = Table(1);
//    tables[10] = Table(1);

    //read the input data from file
    string my_text;
    ifstream input_file("Mission1.txt");
    if(getline (input_file, my_text))
    {
        //check what type of shop we are dealing with (small/large) and define variables based on this
        if(my_text.substr(0, my_text.length()-1) == "Short simulation")
        {
            problem_id = SMALL_SHOP_PROBLEM_ID;
            n_ovens = SMALL_SHOP_N_OVENS;
            timestamps_large_pizza_cooking = SMALL_SHOP_TIMESTAMPS_LARGE_PIZZA;
            timestamps_small_pizza_cooking = SMALL_SHOP_TIMESTAMPS_SMALL_PIZZA;
        }
        else if(my_text.substr(0, my_text.length()-1) == "Long simulation")
        {
            problem_id = LARGE_SHOP_PROBLEM_ID;
            n_ovens = LARGE_SHOP_N_OVENS;
            timestamps_large_pizza_cooking = LARGE_SHOP_TIMESTAMPS_LARGE_PIZZA;
            timestamps_small_pizza_cooking = LARGE_SHOP_TIMESTAMPS_SMALL_PIZZA;
        }

        //read the orders data
        while (getline (input_file, my_text)) {
          // Output the text from the file
          if(my_text != "END") //if it's not the last line yet, process the order data
          {
              string s[5], delimiter = ",";
              int i[5];
              s[0] = my_text.substr(0, my_text.find(delimiter));
              i[0] = stoi(s[0]);
              my_text.erase(0, my_text.find(delimiter) + delimiter.length());
              s[1] = my_text.substr(0, my_text.find(delimiter));
              i[1] = stoi(s[1]);
              my_text.erase(0, my_text.find(delimiter) + delimiter.length());
              s[2] = my_text.substr(0, my_text.find(delimiter));
              i[2] = stoi(s[2]);
              my_text.erase(0, my_text.find(delimiter) + delimiter.length());
              s[3] = my_text.substr(0, my_text.find(delimiter));
              i[3] = stoi(s[3]);
              my_text.erase(0, my_text.find(delimiter) + delimiter.length());
              s[4] = my_text;
              i[4] = stoi(s[4]);
              //cout<<i[0]<<" "<<i[1]<<" "<<i[2]<<" "<<i[3]<<" "<<i[4]<<endl;
              orders.push(Order(i[0], i[1], i[2], i[3], i[4])); //put the order in a queue orders

          }
          else
          {
              break;
          }
        }
    }
    input_file.close(); // Close the file
    //end of reading from file

    total_num_of_orders = orders.size();

    //start threads that manage each oven separately
    thread threads[n_ovens];
    for(int i = 0; i < n_ovens; i++)
    {
        ovens.push_back(Oven());
        threads[i] = thread(manage_oven, i); //save the thread in an array to run join() later at the bottom of main() function
    }

    //start thread to deal with orders and their timestamps
    thread orders_launcher(launch_orders);
    orders_launcher.join();

    //run join on threads that were started during the execution
    for(int i = 0; i < n_ovens; i++)
    {
     threads[i].join();
    }
    for(int i = 0; i < (int)threads_to_be_joined.size(); i++)
    {
        threads_to_be_joined.at(i).join();
    }

    return 0;
}

int find_least_busy_oven()
{
    int min_queue_time = 10000000, index = 0;
    //in this loop we determine the oven that has the minimum total time to cook pizzas in its queue
    for(int i = 0; i < (int)ovens.size(); i++)
    {
        Oven oven = ovens.at(i);
        if(oven.queue_total_time < min_queue_time)
        {
            min_queue_time = oven.queue_total_time;
            index = i;
        }
    }
    return index;
}

void group_of_customers_on_a_table_func(Order order, Table & table, int table_index)
{
    //a thread function that simulates customers from a certain order sitting on a table and eating their pizzas (via usleep function)
    mtx_cout.lock();
    cout<<"Customers of order #"<<order.index<<" are starting to eat on table #"<<table_index<<endl;
    mtx_cout.unlock();

    //time the group will spend eating depends on number of customers, number of pizzas and time for one person to eat a certain pizza
    usleep(((float)(order.n_large_pizzas_ordered * TIMESTAMPS_TO_EAT_LARGE_PIZZA) / order.number_of_customers + (float)(order.n_small_pizzas_ordered * TIMESTAMPS_TO_EAT_SMALL_PIZZA) / order.number_of_customers)* TIMESTAMP_B);


    mtx_tables.lock();
    table.free_spaces += order.number_of_customers; //customers are standing up, so the table has more free seats now (+ number of customers standing up)
    mtx_tables.unlock();

    //order is fully complete
    mtx_cout.lock();
    cout<<"Customers of order #"<<order.index<<" finished eating on table #"<<table_index<<endl;
    mtx_cout.unlock();
}

void find_relevant_table(Order & order)
{
//as all the pizzas of an order are cooked, it should be served
    while(true)
    {
        mtx_cout.lock();
        cout<<"Finding a table for customers of order #"<<order.index<<endl;
        mtx_cout.unlock();
                    //cout<<"order->n_customers = "<<order.number_of_customers<<endl; //debug print

        //Here it is a little bit tricky part. We intend to effectively find a table for this group. We intend to place large groups
        //on tables with large capacity and small groups on tables with small capacity
        int min_dif = 100000, index = 0;
        bool found_free_table = false;
        for(int i = 0; i < 11; i++)
        {
        //table must be able to accomodate the group
        //and we find the table which has minimum difference between its current number of free spots
        //and the number of customers about to be seated
            if(tables[i].free_spaces >= order.number_of_customers and (tables[i].free_spaces - order.number_of_customers) < min_dif)
            {
                index = i;
                found_free_table = true;
                min_dif = tables[i].free_spaces - order.number_of_customers;
            }
        }

        //if such table was found, make the customers seated.
        //i did not further complicate the system with a feature to relocate customers while they are seated.
        if(found_free_table)
        {
            mtx_cout.lock();
            cout<<"Finding a table for customers of order #"<<order.index<<" | "<<"Relevant table was found"<<endl;
            cout<<"Table with index #"<<index<<", has "<<tables[index].free_spaces<<" free spaces. There are "<<order.number_of_customers<<" customers to be seated"<<endl;
            mtx_cout.unlock();

            //make customers seated on this table
            mtx_tables.lock();
            tables[index].free_spaces -= order.number_of_customers;
            mtx_tables.unlock();

            //this thread will simulate customers eating pizzas on their table (with usleep())
            thread t = thread (group_of_customers_on_a_table_func, order, ref(tables[index]), index);

            threads_to_be_joined.push_back(move(t));
            return;
        }
        else
        {
            mtx_cout.lock();
            cout<<"Finding a table for customers of order #"<<order.index<<" | "<<"Relevant table was NOT found"<<endl;
            mtx_cout.unlock();
            //scan tables until they get free
        }
        //index - here the order will be seated
    }
}

void manage_oven(int oven_index)
{
    //concurrently manage each oven
    while(true)
    {
        Oven oven = ovens.at(oven_index);
        if(!oven.pizzas.empty())
        {
            mtx_cout.lock();
            cout<<"Managing Oven #"<<oven_index<<" | "<<"There are pizzas to be cooked"<<endl;
//            cout<<"Managing Oven #"<<oven_index<<" | "<<"Queue size = "<<oven.pizzas.size()<<endl;
            mtx_cout.unlock();
            Pizza pizza = oven.pizzas.front(); //first pizza in the queue of the oven

            //I put this condition to stop it at the last order. The last order is being processes erratically,
            //I guess there is some memory leak related to threads and data structures. I could not fix it yet
            if(pizza.order->index == total_num_of_orders-1)
            {
                cout<<"|-------------Reached the end of simulation (oven #"<<oven_index<<"-------------|"<<endl;
                return;
            }
            else
            {
                oven.status = oven_status_enum::COOKING; //change the status of oven to cooking

                if(pizza.pizza_size == pizza_size_enum::LARGE)
                {
                    mtx_cout.lock();
                    cout<<"Managing Oven #"<<oven_index<<" | "<<"Starting to cook a large pizza from order #"<<pizza.order->index<<endl;
                    mtx_cout.unlock();

                    usleep(timestamps_large_pizza_cooking*TIMESTAMP_B); //time it takes to cook this pizza

                    mtx_cout.lock();
                    cout<<"Managing Oven #"<<oven_index<<" | "<<"Pizza cooked"<<endl;
                    mtx_cout.unlock();

                    mtx_ovens.lock();
                    oven.queue_total_time -= timestamps_large_pizza_cooking*TIMESTAMP_B; //now the oven's total queue time has decreased
                    mtx_ovens.unlock();

                    pizza.status = pizza_status_enum::ready;

                    mtx_orders.lock();
                    pizza.order->increment_large_pizzas_ready(); //increment the number of large pizzas cooked for that order
                    mtx_orders.unlock();
                }
                else if(pizza.pizza_size == pizza_size_enum::SMALL)
                {
                    mtx_cout.lock();
                    cout<<"Managing Oven #"<<oven_index<<" | "<<"Starting to cook a small pizza from order #"<<pizza.order->index<<endl;
                    mtx_cout.unlock();

                    usleep(timestamps_small_pizza_cooking*TIMESTAMP_B);//time it takes to cook this pizza

                    mtx_cout.lock();
                    cout<<"Managing Oven #"<<oven_index<<" | "<<"Pizza cooked"<<endl;
                    mtx_cout.unlock();

                    mtx_ovens.lock();
                    oven.queue_total_time -= timestamps_small_pizza_cooking*TIMESTAMP_B;
                    mtx_ovens.unlock();

                    pizza.status = pizza_status_enum::ready;

                    mtx_orders.lock();
                    pizza.order->increment_small_pizzas_ready(); //increment the number of small pizzas cooked for that order
                    mtx_orders.unlock();
                }
                //debug prints
    //            cout<<"pizza.order->n_large_pizzas_ordered = "<<pizza.order->n_large_pizzas_ordered<<endl;
    //            cout<<"pizza.order->n_large_pizzas_ready = "<<pizza.order->n_large_pizzas_ready<<endl;
    //            cout<<"pizza.order->n_small_pizzas_ready = "<<pizza.order->n_small_pizzas_ready<<endl;
                //after the pizza is cooked, check maybe this was the last non-cooked pizza of the order and the customers can be served
                if(pizza.order->is_order_completed())
                {
                    mtx_cout.lock();
                    cout<<"Managing Oven #"<<oven_index<<" | "<<"Pizza cooked made order completed"<<endl;
                    mtx_cout.unlock();

                    //start looking for a table
                    thread t = thread (find_relevant_table, ref(*(pizza.order)));
                    threads_to_be_joined.push_back(move(t));
                }
                mtx_ovens.lock();
                oven.pizzas.pop();
                mtx_ovens.unlock();
            }
        }
        else
        { //the queue of pizzas for this oven is empty
            mtx_cout.lock();
            cout<<"Managing Oven #"<<oven_index<<" | "<<"NO pizzas to be cooked"<<endl;
            mtx_cout.unlock();
            oven.status = oven_status_enum::EMPTY; //change the status of oven to empty

        }
    }
}

void assign_order_pizzas_to_less_busier_ovens(Order * order)
{
    mtx_cout.lock();
    cout<<"Processing order #"<<order->index<<endl;
    mtx_cout.unlock();
    //there is not much difference in whether we start first from small or large pizzas
    //effectively distribute large pizzas in the order to ovens
    for(int i = order->n_large_pizzas_ordered; i > 0; i--)
    {
        int index = find_least_busy_oven();
        mtx_ovens.lock();
        ovens.at(index).pizzas.push(Pizza(pizza_size_enum::LARGE,order));
        ovens.at(index).queue_total_time += TIMESTAMPS_TO_EAT_LARGE_PIZZA;
        mtx_ovens.unlock();
    }

    //effectively distribute small pizzas in the order to ovens
    for(int i = order->n_small_pizzas_ordered; i > 0; i--)
    {
        int index = find_least_busy_oven();
        mtx_ovens.lock();
        ovens.at(index).pizzas.push(Pizza(pizza_size_enum::SMALL, order));
        ovens.at(index).queue_total_time += TIMESTAMPS_TO_EAT_SMALL_PIZZA;
        mtx_ovens.unlock();
    }
}

void launch_orders()
{
    //each order is received at a certain timestamp, to simulate that, we use this thread function
    int timestamps_a_passed = 0;
    while(!orders.empty())
    {
        usleep(TIMESTAMP_A);
        timestamps_a_passed++;
        while(timestamps_a_passed == orders.front().issue_time)
        {
            mtx_cout.lock();
            cout<<"timestamps_a_passed = "<<timestamps_a_passed<<endl;
            mtx_cout.unlock();
            Order order = orders.front();
            mtx_orders.lock();
            //launch an order in a separate thread now
            assign_order_pizzas_to_less_busier_ovens(&order);
            orders.pop();
            mtx_orders.unlock();
        }
        mtx_cout.lock();
        cout<<"2000ms passed"<<endl;
        mtx_cout.unlock();
    }
}
