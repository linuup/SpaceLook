import requests
import base64

# Baidu API credentials
API_KEY = 'SRBX6XMrRBMM2FHFLAwaDdHc'
SECRET_KEY = 'TF58FgwXY4EGFpQ80zKaYinDCHRWSVot'

# Image path
IMAGE_PATH = r'D:\Desktop\General.png'

def get_access_token():
    """
    Get access token from Baidu API.
    """
    url = 'https://aip.baidubce.com/oauth/2.0/token'
    params = {
        'grant_type': 'client_credentials',
        'client_id': API_KEY,
        'client_secret': SECRET_KEY
    }
    response = requests.post(url, params=params)
    if response.status_code == 200:
        return response.json().get('access_token')
    else:
        raise Exception(f"Failed to get access token: {response.text}")

def recognize_text(image_path, access_token):
    """
    Recognize text from image using Baidu OCR API.
    """
    # Read and encode image
    with open(image_path, 'rb') as f:
        image_data = base64.b64encode(f.read()).decode('utf-8')
    
    url = 'https://aip.baidubce.com/rest/2.0/ocr/v1/general_basic'
    params = {
        'access_token': access_token
    }
    headers = {
        'Content-Type': 'application/x-www-form-urlencoded'
    }
    data = {
        'image': image_data,
        'detect_direction': 'true',
        'probability': 'true'
    }
    
    response = requests.post(url, params=params, headers=headers, data=data)
    return response.json()

if __name__ == '__main__':
    try:
        access_token = get_access_token()
        result = recognize_text(IMAGE_PATH, access_token)
        print("OCR Result:")
        print(result)
    except Exception as e:
        print(f"Error: {e}")