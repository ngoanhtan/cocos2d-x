/****************************************************************************
Copyright (c) 2010-2012 cocos2d-x.org
Copyright (c) 2008-2010 Ricardo Quesada
Copyright (c) 2009      Valentin Milea
Copyright (c) 2011      Zynga Inc.

http://www.cocos2d-x.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
****************************************************************************/
#include "cocoa/CCString.h"
#include "CCNode.h"
#include "support/CCPointExtension.h"
#include "support/TransformUtils.h"
#include "CCCamera.h"
#include "effects/CCGrid.h"
#include "CCDirector.h"
#include "CCScheduler.h"
#include "touch_dispatcher/CCTouch.h"
#include "actions/CCActionManager.h"
#include "script_support/CCScriptSupport.h"
#include "shaders/CCGLProgram.h"
// externals
#include "kazmath/GL/matrix.h"
#include "support/component/CCComponent.h"
#include "support/component/CCComponentContainer.h"

#if CC_NODE_RENDER_SUBPIXEL
#define RENDER_IN_SUBPIXEL
#else
#define RENDER_IN_SUBPIXEL(__ARGS__) (ceil(__ARGS__))
#endif

NS_CC_BEGIN

// XXX: Yes, nodes might have a sort problem once every 15 days if the game runs at 60 FPS and each frame sprites are reordered.
static int s_globalOrderOfArrival = 1;

Node::Node(void)
: _rotationX(0.0f)
, _rotationY(0.0f)
, _scaleX(1.0f)
, _scaleY(1.0f)
, _vertexZ(0.0f)
, _position(PointZero)
, _skewX(0.0f)
, _skewY(0.0f)
, _anchorPointInPoints(PointZero)
, _anchorPoint(PointZero)
, _contentSize(SizeZero)
, _additionalTransform(AffineTransformMakeIdentity())
, _camera(NULL)
// children (lazy allocs)
// lazy alloc
, _grid(NULL)
, _ZOrder(0)
, _children(NULL)
, _parent(NULL)
// "whole screen" objects. like Scenes and Layers, should set _ignoreAnchorPointForPosition to true
, _tag(kNodeTagInvalid)
// userData is always inited as nil
, _userData(NULL)
, _userObject(NULL)
, _shaderProgram(NULL)
, _GLServerState(ccGLServerState(0))
, _orderOfArrival(0)
, _running(false)
, _transformDirty(true)
, _inverseDirty(true)
, _additionalTransformDirty(false)
, _visible(true)
, _ignoreAnchorPointForPosition(false)
, _reorderChildDirty(false)
, _isTransitionFinished(false)
, _scriptHandler(0)
, _updateScriptHandler(0)
, _componentContainer(NULL)
{
    // set default scheduler and actionManager
    Director *director = Director::sharedDirector();
    _actionManager = director->getActionManager();
    _actionManager->retain();
    _scheduler = director->getScheduler();
    _scheduler->retain();

    ScriptEngineProtocol* pEngine = ScriptEngineManager::sharedManager()->getScriptEngine();
    _scriptType = pEngine != NULL ? pEngine->getScriptType() : kScriptTypeNone;
    _componentContainer = new ComponentContainer(this);
}

Node::~Node(void)
{
    CCLOGINFO( "cocos2d: deallocing: %p", this );
    
    unregisterScriptHandler();
    if (_updateScriptHandler)
    {
        ScriptEngineManager::sharedManager()->getScriptEngine()->removeScriptHandler(_updateScriptHandler);
    }

    CC_SAFE_RELEASE(_actionManager);
    CC_SAFE_RELEASE(_scheduler);
    // attributes
    CC_SAFE_RELEASE(_camera);

    CC_SAFE_RELEASE(_grid);
    CC_SAFE_RELEASE(_shaderProgram);
    CC_SAFE_RELEASE(_userObject);

    if(_children && _children->count() > 0)
    {
        Object* child;
        CCARRAY_FOREACH(_children, child)
        {
            Node* pChild = (Node*) child;
            if (pChild)
            {
                pChild->_parent = NULL;
            }
        }
    }

    // children
    CC_SAFE_RELEASE(_children);
    
          // _comsContainer
    _componentContainer->removeAll();
    CC_SAFE_DELETE(_componentContainer);
}

bool Node::init()
{
    return true;
}

float Node::getSkewX()
{
    return _skewX;
}

void Node::setSkewX(float newSkewX)
{
    _skewX = newSkewX;
    _transformDirty = _inverseDirty = true;
}

float Node::getSkewY()
{
    return _skewY;
}

void Node::setSkewY(float newSkewY)
{
    _skewY = newSkewY;

    _transformDirty = _inverseDirty = true;
}

/// zOrder getter
int Node::getZOrder()
{
    return _ZOrder;
}

/// zOrder setter : private method
/// used internally to alter the zOrder variable. DON'T call this method manually 
void Node::_setZOrder(int z)
{
    _ZOrder = z;
}

void Node::setZOrder(int z)
{
    _setZOrder(z);
    if (_parent)
    {
        _parent->reorderChild(this, z);
    }
}

/// vertexZ getter
float Node::getVertexZ()
{
    return _vertexZ;
}


/// vertexZ setter
void Node::setVertexZ(float var)
{
    _vertexZ = var;
}


/// rotation getter
float Node::getRotation()
{
    CCAssert(_rotationX == _rotationY, "CCNode#rotation. RotationX != RotationY. Don't know which one to return");
    return _rotationX;
}

/// rotation setter
void Node::setRotation(float newRotation)
{
    _rotationX = _rotationY = newRotation;
    _transformDirty = _inverseDirty = true;
}

float Node::getRotationX()
{
    return _rotationX;
}

void Node::setRotationX(float fRotationX)
{
    _rotationX = fRotationX;
    _transformDirty = _inverseDirty = true;
}

float Node::getRotationY()
{
    return _rotationY;
}

void Node::setRotationY(float fRotationY)
{
    _rotationY = fRotationY;
    _transformDirty = _inverseDirty = true;
}

/// scale getter
float Node::getScale(void)
{
    CCAssert( _scaleX == _scaleY, "CCNode#scale. ScaleX != ScaleY. Don't know which one to return");
    return _scaleX;
}

/// scale setter
void Node::setScale(float scale)
{
    _scaleX = _scaleY = scale;
    _transformDirty = _inverseDirty = true;
}

/// scaleX getter
float Node::getScaleX()
{
    return _scaleX;
}

/// scaleX setter
void Node::setScaleX(float newScaleX)
{
    _scaleX = newScaleX;
    _transformDirty = _inverseDirty = true;
}

/// scaleY getter
float Node::getScaleY()
{
    return _scaleY;
}

/// scaleY setter
void Node::setScaleY(float newScaleY)
{
    _scaleY = newScaleY;
    _transformDirty = _inverseDirty = true;
}

/// position getter
const Point& Node::getPosition()
{
    return _position;
}

/// position setter
void Node::setPosition(const Point& newPosition)
{
    _position = newPosition;
    _transformDirty = _inverseDirty = true;
}

void Node::getPosition(float* x, float* y)
{
    *x = _position.x;
    *y = _position.y;
}

void Node::setPosition(float x, float y)
{
    setPosition(ccp(x, y));
}

float Node::getPositionX(void)
{
    return _position.x;
}

float Node::getPositionY(void)
{
    return  _position.y;
}

void Node::setPositionX(float x)
{
    setPosition(ccp(x, _position.y));
}

void Node::setPositionY(float y)
{
    setPosition(ccp(_position.x, y));
}

/// children getter
Array* Node::getChildren()
{
    return _children;
}

unsigned int Node::getChildrenCount(void) const
{
    return _children ? _children->count() : 0;
}

/// camera getter: lazy alloc
Camera* Node::getCamera()
{
    if (!_camera)
    {
        _camera = new Camera();
    }
    
    return _camera;
}


/// grid getter
GridBase* Node::getGrid()
{
    return _grid;
}

/// grid setter
void Node::setGrid(GridBase* pGrid)
{
    CC_SAFE_RETAIN(pGrid);
    CC_SAFE_RELEASE(_grid);
    _grid = pGrid;
}


/// isVisible getter
bool Node::isVisible()
{
    return _visible;
}

/// isVisible setter
void Node::setVisible(bool var)
{
    _visible = var;
}

const Point& Node::getAnchorPointInPoints()
{
    return _anchorPointInPoints;
}

/// anchorPoint getter
const Point& Node::getAnchorPoint()
{
    return _anchorPoint;
}

void Node::setAnchorPoint(const Point& point)
{
    if( ! point.equals(_anchorPoint))
    {
        _anchorPoint = point;
        _anchorPointInPoints = ccp(_contentSize.width * _anchorPoint.x, _contentSize.height * _anchorPoint.y );
        _transformDirty = _inverseDirty = true;
    }
}

/// contentSize getter
const Size& Node::getContentSize() const
{
    return _contentSize;
}

void Node::setContentSize(const Size & size)
{
    if ( ! size.equals(_contentSize))
    {
        _contentSize = size;

        _anchorPointInPoints = ccp(_contentSize.width * _anchorPoint.x, _contentSize.height * _anchorPoint.y );
        _transformDirty = _inverseDirty = true;
    }
}

// isRunning getter
bool Node::isRunning()
{
    return _running;
}

/// parent getter
Node * Node::getParent()
{
    return _parent;
}
/// parent setter
void Node::setParent(Node * var)
{
    _parent = var;
}

/// isRelativeAnchorPoint getter
bool Node::isIgnoreAnchorPointForPosition()
{
    return _ignoreAnchorPointForPosition;
}
/// isRelativeAnchorPoint setter
void Node::ignoreAnchorPointForPosition(bool newValue)
{
    if (newValue != _ignoreAnchorPointForPosition) 
    {
		_ignoreAnchorPointForPosition = newValue;
		_transformDirty = _inverseDirty = true;
	}
}

/// tag getter
int Node::getTag() const
{
    return _tag;
}

/// tag setter
void Node::setTag(int var)
{
    _tag = var;
}

/// userData getter
void * Node::getUserData()
{
    return _userData;
}

/// userData setter
void Node::setUserData(void *var)
{
    _userData = var;
}

unsigned int Node::getOrderOfArrival()
{
    return _orderOfArrival;
}

void Node::setOrderOfArrival(unsigned int uOrderOfArrival)
{
    _orderOfArrival = uOrderOfArrival;
}

GLProgram* Node::getShaderProgram()
{
    return _shaderProgram;
}

Object* Node::getUserObject()
{
    return _userObject;
}

ccGLServerState Node::getGLServerState()
{
    return _GLServerState;
}

void Node::setGLServerState(ccGLServerState glServerState)
{
    _GLServerState = glServerState;
}

void Node::setUserObject(Object *pUserObject)
{
    CC_SAFE_RETAIN(pUserObject);
    CC_SAFE_RELEASE(_userObject);
    _userObject = pUserObject;
}

void Node::setShaderProgram(GLProgram *pShaderProgram)
{
    CC_SAFE_RETAIN(pShaderProgram);
    CC_SAFE_RELEASE(_shaderProgram);
    _shaderProgram = pShaderProgram;
}

Rect Node::boundingBox()
{
    Rect rect = CCRectMake(0, 0, _contentSize.width, _contentSize.height);
    return RectApplyAffineTransform(rect, nodeToParentTransform());
}

Node * Node::create(void)
{
	Node * pRet = new Node();
    if (pRet && pRet->init())
    {
        pRet->autorelease();
    }
    else
    {
        CC_SAFE_DELETE(pRet);
    }
	return pRet;
}

void Node::cleanup()
{
    // actions
    this->stopAllActions();
    this->unscheduleAllSelectors();
    
    if ( _scriptType != kScriptTypeNone)
    {
        ScriptEngineManager::sharedManager()->getScriptEngine()->executeNodeEvent(this, kNodeOnCleanup);
    }
    
    // timers
    arrayMakeObjectsPerformSelector(_children, cleanup, Node*);
}


const char* Node::description()
{
    return String::createWithFormat("<Node | Tag = %d>", _tag)->getCString();
}

// lazy allocs
void Node::childrenAlloc(void)
{
    _children = Array::createWithCapacity(4);
    _children->retain();
}

Node* Node::getChildByTag(int aTag)
{
    CCAssert( aTag != kNodeTagInvalid, "Invalid tag");

    if(_children && _children->count() > 0)
    {
        Object* child;
        CCARRAY_FOREACH(_children, child)
        {
            Node* pNode = (Node*) child;
            if(pNode && pNode->_tag == aTag)
                return pNode;
        }
    }
    return NULL;
}

/* "add" logic MUST only be on this method
* If a class want's to extend the 'addChild' behavior it only needs
* to override this method
*/
void Node::addChild(Node *child, int zOrder, int tag)
{    
    CCAssert( child != NULL, "Argument must be non-nil");
    CCAssert( child->_parent == NULL, "child already added. It can't be added again");

    if( ! _children )
    {
        this->childrenAlloc();
    }

    this->insertChild(child, zOrder);

    child->_tag = tag;

    child->setParent(this);
    child->setOrderOfArrival(s_globalOrderOfArrival++);

    if( _running )
    {
        child->onEnter();
        // prevent onEnterTransitionDidFinish to be called twice when a node is added in onEnter
        if (_isTransitionFinished) {
            child->onEnterTransitionDidFinish();
        }
    }
}

void Node::addChild(Node *child, int zOrder)
{
    CCAssert( child != NULL, "Argument must be non-nil");
    this->addChild(child, zOrder, child->_tag);
}

void Node::addChild(Node *child)
{
    CCAssert( child != NULL, "Argument must be non-nil");
    this->addChild(child, child->_ZOrder, child->_tag);
}

void Node::removeFromParent()
{
    this->removeFromParentAndCleanup(true);
}

void Node::removeFromParentAndCleanup(bool cleanup)
{
    if (_parent != NULL)
    {
        _parent->removeChild(this,cleanup);
    } 
}

void Node::removeChild(Node* child)
{
    this->removeChild(child, true);
}

/* "remove" logic MUST only be on this method
* If a class want's to extend the 'removeChild' behavior it only needs
* to override this method
*/
void Node::removeChild(Node* child, bool cleanup)
{
    // explicit nil handling
    if (_children == NULL)
    {
        return;
    }

    if ( _children->containsObject(child) )
    {
        this->detachChild(child,cleanup);
    }
}

void Node::removeChildByTag(int tag)
{
    this->removeChildByTag(tag, true);
}

void Node::removeChildByTag(int tag, bool cleanup)
{
    CCAssert( tag != kNodeTagInvalid, "Invalid tag");

    Node *child = this->getChildByTag(tag);

    if (child == NULL)
    {
        CCLOG("cocos2d: removeChildByTag(tag = %d): child not found!", tag);
    }
    else
    {
        this->removeChild(child, cleanup);
    }
}

void Node::removeAllChildren()
{
    this->removeAllChildrenWithCleanup(true);
}

void Node::removeAllChildrenWithCleanup(bool cleanup)
{
    // not using detachChild improves speed here
    if ( _children && _children->count() > 0 )
    {
        Object* child;
        CCARRAY_FOREACH(_children, child)
        {
            Node* pNode = (Node*) child;
            if (pNode)
            {
                // IMPORTANT:
                //  -1st do onExit
                //  -2nd cleanup
                if(_running)
                {
                    pNode->onExitTransitionDidStart();
                    pNode->onExit();
                }

                if (cleanup)
                {
                    pNode->cleanup();
                }
                // set parent nil at the end
                pNode->setParent(NULL);
            }
        }
        
        _children->removeAllObjects();
    }
    
}

void Node::detachChild(Node *child, bool doCleanup)
{
    // IMPORTANT:
    //  -1st do onExit
    //  -2nd cleanup
    if (_running)
    {
        child->onExitTransitionDidStart();
        child->onExit();
    }

    // If you don't do cleanup, the child's actions will not get removed and the
    // its scheduledSelectors_ dict will not get released!
    if (doCleanup)
    {
        child->cleanup();
    }

    // set parent nil at the end
    child->setParent(NULL);

    _children->removeObject(child);
}


// helper used by reorderChild & add
void Node::insertChild(Node* child, int z)
{
    _reorderChildDirty = true;
    ccArrayAppendObjectWithResize(_children->data, child);
    child->_setZOrder(z);
}

void Node::reorderChild(Node *child, int zOrder)
{
    CCAssert( child != NULL, "Child must be non-nil");
    _reorderChildDirty = true;
    child->setOrderOfArrival(s_globalOrderOfArrival++);
    child->_setZOrder(zOrder);
}

void Node::sortAllChildren()
{
    if (_reorderChildDirty)
    {
        int i,j,length = _children->data->num;
        Node ** x = (Node**)_children->data->arr;
        Node *tempItem;

        // insertion sort
        for(i=1; i<length; i++)
        {
            tempItem = x[i];
            j = i-1;

            //continue moving element downwards while zOrder is smaller or when zOrder is the same but mutatedIndex is smaller
            while(j>=0 && ( tempItem->_ZOrder < x[j]->_ZOrder || ( tempItem->_ZOrder== x[j]->_ZOrder && tempItem->_orderOfArrival < x[j]->_orderOfArrival ) ) )
            {
                x[j+1] = x[j];
                j = j-1;
            }
            x[j+1] = tempItem;
        }

        //don't need to check children recursively, that's done in visit of each child

        _reorderChildDirty = false;
    }
}


 void Node::draw()
 {
     //CCAssert(0);
     // override me
     // Only use- this function to draw your stuff.
     // DON'T draw your stuff outside this method
 }

void Node::visit()
{
    // quick return if not visible. children won't be drawn.
    if (!_visible)
    {
        return;
    }
    kmGLPushMatrix();

     if (_grid && _grid->isActive())
     {
         _grid->beforeDraw();
     }

    this->transform();

    Node* pNode = NULL;
    unsigned int i = 0;

    if(_children && _children->count() > 0)
    {
        sortAllChildren();
        // draw children zOrder < 0
        ccArray *arrayData = _children->data;
        for( ; i < arrayData->num; i++ )
        {
            pNode = (Node*) arrayData->arr[i];

            if ( pNode && pNode->_ZOrder < 0 ) 
            {
                pNode->visit();
            }
            else
            {
                break;
            }
        }
        // self draw
        this->draw();

        for( ; i < arrayData->num; i++ )
        {
            pNode = (Node*) arrayData->arr[i];
            if (pNode)
            {
                pNode->visit();
            }
        }        
    }
    else
    {
        this->draw();
    }

    // reset for next frame
    _orderOfArrival = 0;

     if (_grid && _grid->isActive())
     {
         _grid->afterDraw(this);
    }
 
    kmGLPopMatrix();
}

void Node::transformAncestors()
{
    if( _parent != NULL  )
    {
        _parent->transformAncestors();
        _parent->transform();
    }
}

void Node::transform()
{    
    kmMat4 transfrom4x4;

    // Convert 3x3 into 4x4 matrix
    AffineTransform tmpAffine = this->nodeToParentTransform();
    CGAffineToGL(&tmpAffine, transfrom4x4.mat);

    // Update Z vertex manually
    transfrom4x4.mat[14] = _vertexZ;

    kmGLMultMatrix( &transfrom4x4 );


    // XXX: Expensive calls. Camera should be integrated into the cached affine matrix
    if ( _camera != NULL && !(_grid != NULL && _grid->isActive()) )
    {
        bool translate = (_anchorPointInPoints.x != 0.0f || _anchorPointInPoints.y != 0.0f);

        if( translate )
            kmGLTranslatef(RENDER_IN_SUBPIXEL(_anchorPointInPoints.x), RENDER_IN_SUBPIXEL(_anchorPointInPoints.y), 0 );

        _camera->locate();

        if( translate )
            kmGLTranslatef(RENDER_IN_SUBPIXEL(-_anchorPointInPoints.x), RENDER_IN_SUBPIXEL(-_anchorPointInPoints.y), 0 );
    }

}


void Node::onEnter()
{
    _isTransitionFinished = false;

    arrayMakeObjectsPerformSelector(_children, onEnter, Node*);

    this->resumeSchedulerAndActions();

    _running = true;

    if (_scriptType != kScriptTypeNone)
    {
        ScriptEngineManager::sharedManager()->getScriptEngine()->executeNodeEvent(this, kNodeOnEnter);
    }
}

void Node::onEnterTransitionDidFinish()
{
    _isTransitionFinished = true;

    arrayMakeObjectsPerformSelector(_children, onEnterTransitionDidFinish, Node*);

    if (_scriptType == kScriptTypeJavascript)
    {
        ScriptEngineManager::sharedManager()->getScriptEngine()->executeNodeEvent(this, kNodeOnEnterTransitionDidFinish);
    }
}

void Node::onExitTransitionDidStart()
{
    arrayMakeObjectsPerformSelector(_children, onExitTransitionDidStart, Node*);

    if (_scriptType == kScriptTypeJavascript)
    {
        ScriptEngineManager::sharedManager()->getScriptEngine()->executeNodeEvent(this, kNodeOnExitTransitionDidStart);
    }
}

void Node::onExit()
{
    this->pauseSchedulerAndActions();

    _running = false;

    if ( _scriptType != kScriptTypeNone)
    {
        ScriptEngineManager::sharedManager()->getScriptEngine()->executeNodeEvent(this, kNodeOnExit);
    }

    arrayMakeObjectsPerformSelector(_children, onExit, Node*);    
}

void Node::registerScriptHandler(int nHandler)
{
    unregisterScriptHandler();
    _scriptHandler = nHandler;
    LUALOG("[LUA] Add Node event handler: %d", _scriptHandler);
}

void Node::unregisterScriptHandler(void)
{
    if (_scriptHandler)
    {
        ScriptEngineManager::sharedManager()->getScriptEngine()->removeScriptHandler(_scriptHandler);
        LUALOG("[LUA] Remove Node event handler: %d", _scriptHandler);
        _scriptHandler = 0;
    }
}

void Node::setActionManager(ActionManager* actionManager)
{
    if( actionManager != _actionManager ) {
        this->stopAllActions();
        CC_SAFE_RETAIN(actionManager);
        CC_SAFE_RELEASE(_actionManager);
        _actionManager = actionManager;
    }
}

ActionManager* Node::getActionManager()
{
    return _actionManager;
}

Action * Node::runAction(Action* action)
{
    CCAssert( action != NULL, "Argument must be non-nil");
    _actionManager->addAction(action, this, !_running);
    return action;
}

void Node::stopAllActions()
{
    _actionManager->removeAllActionsFromTarget(this);
}

void Node::stopAction(Action* action)
{
    _actionManager->removeAction(action);
}

void Node::stopActionByTag(int tag)
{
    CCAssert( tag != kActionTagInvalid, "Invalid tag");
    _actionManager->removeActionByTag(tag, this);
}

Action * Node::getActionByTag(int tag)
{
    CCAssert( tag != kActionTagInvalid, "Invalid tag");
    return _actionManager->getActionByTag(tag, this);
}

unsigned int Node::numberOfRunningActions()
{
    return _actionManager->numberOfRunningActionsInTarget(this);
}

// Node - Callbacks

void Node::setScheduler(Scheduler* scheduler)
{
    if( scheduler != _scheduler ) {
        this->unscheduleAllSelectors();
        CC_SAFE_RETAIN(scheduler);
        CC_SAFE_RELEASE(_scheduler);
        _scheduler = scheduler;
    }
}

Scheduler* Node::getScheduler()
{
    return _scheduler;
}

void Node::scheduleUpdate()
{
    scheduleUpdateWithPriority(0);
}

void Node::scheduleUpdateWithPriority(int priority)
{
    _scheduler->scheduleUpdateForTarget(this, priority, !_running);
}

void Node::scheduleUpdateWithPriorityLua(int nHandler, int priority)
{
    unscheduleUpdate();
    _updateScriptHandler = nHandler;
    _scheduler->scheduleUpdateForTarget(this, priority, !_running);
}

void Node::unscheduleUpdate()
{
    _scheduler->unscheduleUpdateForTarget(this);
    if (_updateScriptHandler)
    {
        ScriptEngineManager::sharedManager()->getScriptEngine()->removeScriptHandler(_updateScriptHandler);
        _updateScriptHandler = 0;
    }
}

void Node::schedule(SEL_SCHEDULE selector)
{
    this->schedule(selector, 0.0f, kRepeatForever, 0.0f);
}

void Node::schedule(SEL_SCHEDULE selector, float interval)
{
    this->schedule(selector, interval, kRepeatForever, 0.0f);
}

void Node::schedule(SEL_SCHEDULE selector, float interval, unsigned int repeat, float delay)
{
    CCAssert( selector, "Argument must be non-nil");
    CCAssert( interval >=0, "Argument must be positive");

    _scheduler->scheduleSelector(selector, this, interval , repeat, delay, !_running);
}

void Node::scheduleOnce(SEL_SCHEDULE selector, float delay)
{
    this->schedule(selector, 0.0f, 0, delay);
}

void Node::unschedule(SEL_SCHEDULE selector)
{
    // explicit nil handling
    if (selector == 0)
        return;

    _scheduler->unscheduleSelector(selector, this);
}

void Node::unscheduleAllSelectors()
{
    _scheduler->unscheduleAllForTarget(this);
}

void Node::resumeSchedulerAndActions()
{
    _scheduler->resumeTarget(this);
    _actionManager->resumeTarget(this);
}

void Node::pauseSchedulerAndActions()
{
    _scheduler->pauseTarget(this);
    _actionManager->pauseTarget(this);
}

// override me
void Node::update(float fDelta)
{
    if (_updateScriptHandler)
    {
        ScriptEngineManager::sharedManager()->getScriptEngine()->executeSchedule(_updateScriptHandler, fDelta, this);
    }
    
    if (_componentContainer && !_componentContainer->isEmpty())
    {
        _componentContainer->visit(fDelta);
    }
}

AffineTransform Node::nodeToParentTransform(void)
{
    if (_transformDirty) 
    {

        // Translate values
        float x = _position.x;
        float y = _position.y;

        if (_ignoreAnchorPointForPosition) 
        {
            x += _anchorPointInPoints.x;
            y += _anchorPointInPoints.y;
        }

        // Rotation values
		// Change rotation code to handle X and Y
		// If we skew with the exact same value for both x and y then we're simply just rotating
        float cx = 1, sx = 0, cy = 1, sy = 0;
        if (_rotationX || _rotationY)
        {
            float radiansX = -CC_DEGREES_TO_RADIANS(_rotationX);
            float radiansY = -CC_DEGREES_TO_RADIANS(_rotationY);
            cx = cosf(radiansX);
            sx = sinf(radiansX);
            cy = cosf(radiansY);
            sy = sinf(radiansY);
        }

        bool needsSkewMatrix = ( _skewX || _skewY );


        // optimization:
        // inline anchor point calculation if skew is not needed
        // Adjusted transform calculation for rotational skew
        if (! needsSkewMatrix && !_anchorPointInPoints.equals(PointZero))
        {
            x += cy * -_anchorPointInPoints.x * _scaleX + -sx * -_anchorPointInPoints.y * _scaleY;
            y += sy * -_anchorPointInPoints.x * _scaleX +  cx * -_anchorPointInPoints.y * _scaleY;
        }


        // Build Transform Matrix
        // Adjusted transform calculation for rotational skew
        _transform = AffineTransformMake( cy * _scaleX,  sy * _scaleX,
            -sx * _scaleY, cx * _scaleY,
            x, y );

        // XXX: Try to inline skew
        // If skew is needed, apply skew and then anchor point
        if (needsSkewMatrix) 
        {
            AffineTransform skewMatrix = AffineTransformMake(1.0f, tanf(CC_DEGREES_TO_RADIANS(_skewY)),
                tanf(CC_DEGREES_TO_RADIANS(_skewX)), 1.0f,
                0.0f, 0.0f );
            _transform = AffineTransformConcat(skewMatrix, _transform);

            // adjust anchor point
            if (!_anchorPointInPoints.equals(PointZero))
            {
                _transform = AffineTransformTranslate(_transform, -_anchorPointInPoints.x, -_anchorPointInPoints.y);
            }
        }
        
        if (_additionalTransformDirty)
        {
            _transform = AffineTransformConcat(_transform, _additionalTransform);
            _additionalTransformDirty = false;
        }

        _transformDirty = false;
    }

    return _transform;
}

void Node::setAdditionalTransform(const AffineTransform& additionalTransform)
{
    _additionalTransform = additionalTransform;
    _transformDirty = true;
    _additionalTransformDirty = true;
}

AffineTransform Node::parentToNodeTransform(void)
{
    if ( _inverseDirty ) {
        _inverse = AffineTransformInvert(this->nodeToParentTransform());
        _inverseDirty = false;
    }

    return _inverse;
}

AffineTransform Node::nodeToWorldTransform()
{
    AffineTransform t = this->nodeToParentTransform();

    for (Node *p = _parent; p != NULL; p = p->getParent())
        t = AffineTransformConcat(t, p->nodeToParentTransform());

    return t;
}

AffineTransform Node::worldToNodeTransform(void)
{
    return AffineTransformInvert(this->nodeToWorldTransform());
}

Point Node::convertToNodeSpace(const Point& worldPoint)
{
    Point ret = PointApplyAffineTransform(worldPoint, worldToNodeTransform());
    return ret;
}

Point Node::convertToWorldSpace(const Point& nodePoint)
{
    Point ret = PointApplyAffineTransform(nodePoint, nodeToWorldTransform());
    return ret;
}

Point Node::convertToNodeSpaceAR(const Point& worldPoint)
{
    Point nodePoint = convertToNodeSpace(worldPoint);
    return ccpSub(nodePoint, _anchorPointInPoints);
}

Point Node::convertToWorldSpaceAR(const Point& nodePoint)
{
    Point pt = ccpAdd(nodePoint, _anchorPointInPoints);
    return convertToWorldSpace(pt);
}

Point Node::convertToWindowSpace(const Point& nodePoint)
{
    Point worldPoint = this->convertToWorldSpace(nodePoint);
    return Director::sharedDirector()->convertToUI(worldPoint);
}

// convenience methods which take a Touch instead of Point
Point Node::convertTouchToNodeSpace(Touch *touch)
{
    Point point = touch->getLocation();
    return this->convertToNodeSpace(point);
}
Point Node::convertTouchToNodeSpaceAR(Touch *touch)
{
    Point point = touch->getLocation();
    return this->convertToNodeSpaceAR(point);
}

void Node::updateTransform()
{
    // Recursively iterate over children
    arrayMakeObjectsPerformSelector(_children, updateTransform, Node*);
}

Component* Node::getComponent(const char *pName) const
{
    return _componentContainer->get(pName);
}

bool Node::addComponent(Component *pComponent)
{
    return _componentContainer->add(pComponent);
}

bool Node::removeComponent(const char *pName)
{
    return _componentContainer->remove(pName);
}

void Node::removeAllComponents()
{
    _componentContainer->removeAll();
}

// NodeRGBA
NodeRGBA::NodeRGBA()
: _displayedOpacity(255)
, _realOpacity(255)
, _displayedColor(ccWHITE)
, _realColor(ccWHITE)
, _cascadeColorEnabled(false)
, _cascadeOpacityEnabled(false)
{}

NodeRGBA::~NodeRGBA() {}

bool NodeRGBA::init()
{
    if (Node::init())
    {
        _displayedOpacity = _realOpacity = 255;
        _displayedColor = _realColor = ccWHITE;
        _cascadeOpacityEnabled = _cascadeColorEnabled = false;
        return true;
    }
    return false;
}

GLubyte NodeRGBA::getOpacity(void)
{
	return _realOpacity;
}

GLubyte NodeRGBA::getDisplayedOpacity(void)
{
	return _displayedOpacity;
}

void NodeRGBA::setOpacity(GLubyte opacity)
{
    _displayedOpacity = _realOpacity = opacity;
    
	if (_cascadeOpacityEnabled)
    {
		GLubyte parentOpacity = 255;
        RGBAProtocol* pParent = dynamic_cast<RGBAProtocol*>(_parent);
        if (pParent && pParent->isCascadeOpacityEnabled())
        {
            parentOpacity = pParent->getDisplayedOpacity();
        }
        this->updateDisplayedOpacity(parentOpacity);
	}
}

void NodeRGBA::updateDisplayedOpacity(GLubyte parentOpacity)
{
	_displayedOpacity = _realOpacity * parentOpacity/255.0;
	
    if (_cascadeOpacityEnabled)
    {
        Object* pObj;
        CCARRAY_FOREACH(_children, pObj)
        {
            RGBAProtocol* item = dynamic_cast<RGBAProtocol*>(pObj);
            if (item)
            {
                item->updateDisplayedOpacity(_displayedOpacity);
            }
        }
    }
}

bool NodeRGBA::isCascadeOpacityEnabled(void)
{
    return _cascadeOpacityEnabled;
}

void NodeRGBA::setCascadeOpacityEnabled(bool cascadeOpacityEnabled)
{
    _cascadeOpacityEnabled = cascadeOpacityEnabled;
}

const ccColor3B& NodeRGBA::getColor(void)
{
	return _realColor;
}

const ccColor3B& NodeRGBA::getDisplayedColor()
{
	return _displayedColor;
}

void NodeRGBA::setColor(const ccColor3B& color)
{
	_displayedColor = _realColor = color;
	
	if (_cascadeColorEnabled)
    {
		ccColor3B parentColor = ccWHITE;
        RGBAProtocol *parent = dynamic_cast<RGBAProtocol*>(_parent);
		if (parent && parent->isCascadeColorEnabled())
        {
            parentColor = parent->getDisplayedColor(); 
        }
        
        updateDisplayedColor(parentColor);
	}
}

void NodeRGBA::updateDisplayedColor(const ccColor3B& parentColor)
{
	_displayedColor.r = _realColor.r * parentColor.r/255.0;
	_displayedColor.g = _realColor.g * parentColor.g/255.0;
	_displayedColor.b = _realColor.b * parentColor.b/255.0;
    
    if (_cascadeColorEnabled)
    {
        Object *obj = NULL;
        CCARRAY_FOREACH(_children, obj)
        {
            RGBAProtocol *item = dynamic_cast<RGBAProtocol*>(obj);
            if (item)
            {
                item->updateDisplayedColor(_displayedColor);
            }
        }
    }
}

bool NodeRGBA::isCascadeColorEnabled(void)
{
    return _cascadeColorEnabled;
}

void NodeRGBA::setCascadeColorEnabled(bool cascadeColorEnabled)
{
    _cascadeColorEnabled = cascadeColorEnabled;
}

NS_CC_END
