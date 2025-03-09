/*
* Copyright (c) 2025 Eugene Cohen
* Copyright (c) 2017 Jason Lowe-Power
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met: redistributions of source code must retain the above copyright
* notice, this list of conditions and the following disclaimer;
* redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution;
* neither the name of the copyright holders nor the names of its
* contributors may be used to endorse or promote products derived from
* this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
* A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
* OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
* DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
* THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* thrash caches with a pathological false sharing workload, each thread
 * increments a 32-bit quantity in a packed array.
 */

#include <iostream>
#include <thread>

using namespace std;

/*
 * a[tid] = a[tid]+1
 */
void do_increments(volatile uint32_t *a, int num_values)
{
    for (int i=0; i<num_values; i++) {
        *a = *a + 1;
    }
}

int main(int argc, char *argv[])
{
    unsigned int num_values = 10000;
    unsigned int data_stride = 1;

    if (argc == 2) {
        data_stride = atoi(argv[1]);
        if (data_stride <= 0) {
            cerr << "Usage: " << argv[0] << " [data_stride]" << endl;
            return 1;
        }
    } else {
        if (argc > 2) {
            cerr << "Usage: " << argv[0] << " [data_stride]" << endl;
            return 1;
        }
    }

    unsigned num_cpus = thread::hardware_concurrency();

    cout << "Running on " << num_cpus << " cores. ";
    cout << "with " << num_values << " values ";
    cout << "and data stride of " << data_stride << " uint32_t values" << endl;

    uint32_t *a = new uint32_t[num_cpus * data_stride];

    if (!a) {
        cerr << "Allocation error!" << endl;
        return 2;
    }

    for (int i=0; i<num_cpus; i++) {
        a[i] = 0;
        cout << "a[i] at address 0x " << std::hex
            << (uintptr_t)&a[i * data_stride] << endl;
    }

    thread **threads = new thread*[num_cpus];

    // NOTE: -1 is required for this to work in SE mode.
    for (int i=0; i<num_cpus-1; i++) {
        threads[i] =
            new thread(do_increments, &a[i * data_stride], num_values);
    }
    // Execute the last thread with this thread context to appease SE mode
    do_increments(&a[ (num_cpus-1) * data_stride], num_values);

    cout << "Waiting for other threads to complete" << endl;

    for (int i=0; i<num_cpus - 1; i++) {
        threads[i]->join();
    }

    delete[] threads;

    cout << "Validating..." << flush;

    int num_valid = 0;
    for (int i=0; i<num_cpus; i++) {
        if (a[i*data_stride] == num_values) {
            num_valid++;
        } else {
            cerr << "a[" << i << "] is wrong.";
            cerr << " Expected " << num_values;
            cerr << " Got " << a[i*data_stride] << "." << endl;
        }
    }

    if (num_valid == num_cpus) {
        cout << "Success!" << endl;
        return 0;
    } else {
        return 2;
    }
}
