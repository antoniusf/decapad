from copy import deepcopy

state = { "frontend": "ls", "backend": "ls", "frontend to backend": [], "backend to frontend": []}
possible_states = [[state, False]]

def add_possible_state(state):

    if [state, True] not in possible_states and [state, False] not in possible_states:
        possible_states.append([state, False])

fe_transitions = [ { "begin": "ls", "end": "ls", "push": "c",  "pop": None },
                   { "begin": "ls", "end": "us", "push": "u",  "pop": None },
                   { "begin": "us", "end": "us", "push": "i",  "pop": None },
                   { "begin": "us", "end": "us", "push": None, "pop": "r"  },
                   { "begin": "us", "end": "sy", "push": "b",  "pop": None },
                   { "begin": "sy", "end": "sy", "push": None, "pop": "r"  },
                   { "begin": "sy", "end": "us", "push": None, "pop": "f"  },
                   { "begin": "sy", "end": "ls", "push": None, "pop": "s"  } ]

be_transitions = [ { "begin": "ls", "end": "ls", "push": None, "pop": "c"  },
                   { "begin": "ls", "end": "us", "push": None, "pop": "u"  },
                   { "begin": "us", "end": "us", "push": None, "pop": "i"  },
                   { "begin": "us", "end": "sy", "push": None, "pop": "b"  },
                   { "begin": "us", "end": "us", "push": "r" , "pop": None },
                   { "begin": "sy", "end": "us", "push": "f" , "pop": None },
                   { "begin": "sy", "end": "ls", "push": "s" , "pop": None } ]



def push(msg, queue):

    if len(queue) > 0:
        if queue[-1] == msg:
            queue[-1] = msg+"*" #two or more
        elif queue[-1] == msg+"*":
            pass
        else:
            queue.append(msg)
    else:
        queue.append(msg)

def fe_transition(state):

    queue_value_taken = True
    if len(state["backend to frontend"]) > 0:
        queue_value_taken = False

    successor_state_found = False

    for transition in fe_transitions:

        if transition["begin"] == state["frontend"]:

            new_state = deepcopy(state)
            new_state["frontend"] = transition["end"]
            
            if transition["push"]:
                push(transition["push"], new_state["frontend to backend"])

            if transition["pop"]:
                
                if len(new_state["backend to frontend"]) > 0:

                    if new_state["backend to frontend"][0] == transition["pop"]+"*":
                        add_possible_state(new_state)
                        new_state = deepcopy(new_state)
                        new_state["backend to frontend"][0] = transition["pop"] #remove the star
                        add_possible_state(new_state)
                        queue_value_taken = True
                        successor_state_found = True

                    elif new_state["backend to frontend"][0] == transition["pop"]:
                        del new_state["backend to frontend"][0]
                        add_possible_state(new_state)
                        queue_value_taken = True
                        successor_state_found = True

                    else:
                        pass #transition doesn't match

                else:
                    pass #transition doesn't match

            else:
                add_possible_state(new_state)
                successor_state_found = True

    if queue_value_taken == False:
        print "FAIL!\n  Frontend received message: "+str(state["backend to frontend"][0])+"\n  in state: "+str(state["frontend"])+"\n"

    return successor_state_found

def be_transition(state):

    queue_value_taken = True
    if len(state["frontend to backend"]) > 0:
        queue_value_taken = False

    successor_state_found = False

    for transition in be_transitions:

        if transition["begin"] == state["backend"]:

            new_state = deepcopy(state)
            new_state["backend"] = transition["end"]
            
            if transition["push"]:
                push(transition["push"], new_state["backend to frontend"])

            if transition["pop"]:
                
                if len(new_state["frontend to backend"]) > 0:

                    if new_state["frontend to backend"][0] == transition["pop"]+"*":
                        add_possible_state(new_state)
                        new_state = deepcopy(new_state)
                        new_state["frontend to backend"][0] = transition["pop"] #remove the star
                        add_possible_state(new_state)
                        queue_value_taken = True
                        successor_state_found = True

                    elif new_state["frontend to backend"][0] == transition["pop"]:
                        del new_state["frontend to backend"][0]
                        add_possible_state(new_state)
                        queue_value_taken = True
                        successor_state_found = True

                    else:
                        pass #transition doesn't match

                else:
                    pass #transition doesn't match

            else:
                add_possible_state(new_state)
                successor_state_found = True

    if queue_value_taken == False:
        print "FAIL!\n  Backend received message: "+str(state["frontend to backend"][0])+"\n  in state: "+str(state["backend"])+"\n"

    return successor_state_found


for state in possible_states:

    if state[1] == False:

        fe_successor = fe_transition(state[0])
        be_successor = be_transition(state[0])
        if not fe_successor and not be_successor:
            print "FAIL!\nNo successor state found to: "+str(state[0])+"\n"
        state[1] = True

possible_state_combinations = []

for state in possible_states:

    new_combo = (state[0]["frontend"], state[0]["backend"])
    if new_combo not in possible_state_combinations:
        possible_state_combinations.append(new_combo)
        print "Frontend: "+new_combo[0]+", Backend: "+new_combo[1]

print possible_states
